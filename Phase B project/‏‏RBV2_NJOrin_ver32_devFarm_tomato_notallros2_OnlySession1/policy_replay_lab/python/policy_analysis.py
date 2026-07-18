#!/usr/bin/env python3
"""
RBV2 ver32 policy replay analysis - v3.

Input:  cpp_replay_detections.jsonl created by policy_replay_cli.
Output: policy_compare.csv, heuristics.jsonl, summary.json, debug images.

This analysis layer intentionally does not change the TensorRT/YOLO model.
It adds experimental post-policy evidence layers for research:

1) Yellow Heuristics bunch candidates from weak bunch anchors with child support.
2) Color-based class correction for single tomatoes when segmentation color
   strongly contradicts the model maturity class.
3) Review/Candidate status for high-quality near-threshold detections.

The goal is to compare old robot policy vs. a more explainable CV policy on
existing greenhouse sessions, without training and without model conversion.
"""

from __future__ import annotations

import argparse
import csv
import json
from collections import Counter, defaultdict
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional, Tuple

try:
    import cv2  # type: ignore
except Exception as exc:  # pragma: no cover
    cv2 = None
    CV2_IMPORT_ERROR = exc
else:
    CV2_IMPORT_ERROR = None

Detection = Dict[str, Any]
BBox = Dict[str, float]


REQUESTED_CSV_FIELDS = [
    "session_id",
    "image_path",
    "image_group",
    "frame_id",
    "detection_id",
    "source_type",
    "class_id",
    "class_name",
    "confidence",
    "old_status",
    "new_status",
    "old_reject_reason",
    "new_reject_reason",
    "original_class_id",
    "original_class_name",
    "new_class_id",
    "new_class_name",
    "corrected_class_id",
    "corrected_class_name",
    "class_corrected_by_color",
    "correction_reason",
    "color_correction_score",
    "review_candidate",
    "review_reason",
    "mask_density",
    "mask_area",
    "box_area",
    "red_ratio",
    "orange_ratio",
    "warm_ratio",
    "green_yellow_ratio",
    "bbox_x",
    "bbox_y",
    "bbox_w",
    "bbox_h",
    "promoted_by_policy",
    "promotion_reason",
    "heuristic_type",
    "anchor_detection_id",
    "anchor_bunch_class",
    "anchor_bunch_confidence",
    "child_detection_ids",
    "child_count",
    "weak_child_count",
    "strong_child_count",
    "weighted_child_count",
    "ripe_evidence_score",
    "unripe_evidence_score",
    "dominant_maturity",
    "heuristic_score",
    "bbox_source",
]

CLASS_NAME_BY_ID = {
    0: "eripe bunch",
    1: "ripe",
    2: "unripe",
    3: "unripe bunch",
    100: "HEURISTIC ripe bunch",
    103: "HEURISTIC unripe bunch",
    104: "HEURISTIC mixed bunch",
}


def load_json(path: Path) -> Dict[str, Any]:
    with path.open("r", encoding="utf-8") as f:
        return json.load(f)


def load_jsonl(path: Path) -> List[Detection]:
    rows: List[Detection] = []
    with path.open("r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            rows.append(json.loads(line))
    return rows


def write_jsonl(path: Path, rows: Iterable[Detection]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as f:
        for row in rows:
            f.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")


def write_csv(path: Path, rows: List[Dict[str, Any]], fields: List[str]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)


def as_float(v: Any, default: float = 0.0) -> float:
    try:
        if v is None or v == "":
            return default
        return float(v)
    except Exception:
        return default


def as_int(v: Any, default: int = 0) -> int:
    try:
        if v is None or v == "":
            return default
        return int(v)
    except Exception:
        return default


def bbox_of(d: Detection) -> BBox:
    if "bbox" in d and isinstance(d["bbox"], dict):
        b = d["bbox"]
        return {
            "x": as_float(b.get("x")),
            "y": as_float(b.get("y")),
            "w": as_float(b.get("w")),
            "h": as_float(b.get("h")),
        }
    return {
        "x": as_float(d.get("bbox_x")),
        "y": as_float(d.get("bbox_y")),
        "w": as_float(d.get("bbox_w")),
        "h": as_float(d.get("bbox_h")),
    }


def metrics_of(d: Detection) -> Dict[str, float]:
    m = d.get("metrics") if isinstance(d.get("metrics"), dict) else {}
    return {
        "box_area": as_float(m.get("box_area", d.get("box_area"))),
        "mask_area": as_float(m.get("mask_area", d.get("mask_area"))),
        "mask_density": as_float(m.get("mask_density", d.get("mask_density"))),
        "red_ratio": as_float(m.get("red_ratio", d.get("red_ratio"))),
        "orange_ratio": as_float(m.get("orange_ratio", d.get("orange_ratio"))),
        "warm_ratio": as_float(m.get("warm_ratio", d.get("warm_ratio"))),
        "green_yellow_ratio": as_float(m.get("green_yellow_ratio", d.get("green_yellow_ratio"))),
    }


def class_name_for(class_id: int, fallback: str = "") -> str:
    return CLASS_NAME_BY_ID.get(class_id, fallback or str(class_id))


def bbox_iou(a: BBox, b: BBox) -> float:
    ax1, ay1, ax2, ay2 = a["x"], a["y"], a["x"] + a["w"], a["y"] + a["h"]
    bx1, by1, bx2, by2 = b["x"], b["y"], b["x"] + b["w"], b["y"] + b["h"]
    ix1, iy1 = max(ax1, bx1), max(ay1, by1)
    ix2, iy2 = min(ax2, bx2), min(ay2, by2)
    iw, ih = max(0.0, ix2 - ix1), max(0.0, iy2 - iy1)
    inter = iw * ih
    union = max(1e-6, a["w"] * a["h"] + b["w"] * b["h"] - inter)
    return inter / union


def clamp_bbox(b: BBox, image_w: Optional[int] = None, image_h: Optional[int] = None) -> BBox:
    x1 = b["x"]
    y1 = b["y"]
    x2 = b["x"] + b["w"]
    y2 = b["y"] + b["h"]
    if image_w is not None and image_w > 1:
        x1 = max(0.0, min(float(image_w - 1), x1))
        x2 = max(0.0, min(float(image_w), x2))
    if image_h is not None and image_h > 1:
        y1 = max(0.0, min(float(image_h - 1), y1))
        y2 = max(0.0, min(float(image_h), y2))
    return {"x": x1, "y": y1, "w": max(1.0, x2 - x1), "h": max(1.0, y2 - y1)}


def expand_bbox(b: BBox, ratio: float, image_w: Optional[int] = None, image_h: Optional[int] = None) -> BBox:
    pad_x = b["w"] * ratio
    pad_y = b["h"] * ratio
    out = {
        "x": b["x"] - pad_x,
        "y": b["y"] - pad_y,
        "w": b["w"] + pad_x * 2.0,
        "h": b["h"] + pad_y * 2.0,
    }
    return clamp_bbox(out, image_w=image_w, image_h=image_h)


def union_bboxes(boxes: List[BBox], padding_ratio: float = 0.0, image_w: Optional[int] = None, image_h: Optional[int] = None) -> BBox:
    x1 = min(b["x"] for b in boxes)
    y1 = min(b["y"] for b in boxes)
    x2 = max(b["x"] + b["w"] for b in boxes)
    y2 = max(b["y"] + b["h"] for b in boxes)
    b = {"x": x1, "y": y1, "w": max(1.0, x2 - x1), "h": max(1.0, y2 - y1)}
    return expand_bbox(b, padding_ratio, image_w=image_w, image_h=image_h)


def center_inside(child: Detection, parent_box: BBox) -> bool:
    b = bbox_of(child)
    cx = b["x"] + b["w"] * 0.5
    cy = b["y"] + b["h"] * 0.5
    return parent_box["x"] <= cx <= parent_box["x"] + parent_box["w"] and parent_box["y"] <= cy <= parent_box["y"] + parent_box["h"]


def reject_reason(d: Detection) -> str:
    return str(d.get("old_reject_reason") or d.get("reject_reason") or "")


def has_reason(d: Detection, token: str) -> bool:
    return token in reject_reason(d)


def is_bunch_class(d: Detection) -> bool:
    return as_int(d.get("class_id"), -1) in (0, 3)


def is_single_class(d: Detection) -> bool:
    return as_int(d.get("class_id"), -1) in (1, 2)


def is_ripe_class(d: Detection) -> bool:
    return as_int(d.get("class_id"), -1) in (0, 1)


def is_unripe_class(d: Detection) -> bool:
    return as_int(d.get("class_id"), -1) in (2, 3)


def classify_model_status(d: Detection, cfg: Dict[str, Any]) -> str:
    """Old-policy rendering class: strong / weak / noise.

    This does not include new review or color correction. It is used to keep the
    original robot policy visible and comparable.
    """
    if str(d.get("old_status")) == "strong" or not bool(d.get("weak", False)):
        return "strong"
    conf = as_float(d.get("confidence"))
    reason = reject_reason(d)
    metrics = metrics_of(d)
    noise_cfg = cfg.get("noise_classification", {})
    if conf < as_float(noise_cfg.get("weak_noise_conf_below", 0.30)):
        return "noise"
    if noise_cfg.get("treat_tomato_sanity_failed_as_noise", True) and "tomato_sanity_failed" in reason:
        return "noise"
    if is_single_class(d) and metrics["mask_density"] > 0 and metrics["mask_density"] < as_float(noise_cfg.get("severe_single_mask_density_below", 0.06)):
        return "noise"
    if is_bunch_class(d) and metrics["mask_density"] > 0 and metrics["mask_density"] < as_float(noise_cfg.get("severe_bunch_mask_density_below", 0.020)):
        return "noise"
    return "weak"


def color_support_for_child(d: Detection, target: str, cfg: Dict[str, Any]) -> float:
    hc = cfg.get("heuristics", {})
    m = metrics_of(d)
    if target == "ripe":
        red_ref = max(1e-6, as_float(hc.get("ripe_red_ratio_reference", 0.08)))
        warm_ref = max(1e-6, as_float(hc.get("ripe_warm_ratio_reference", 0.12)))
        return min(1.0, max(m["red_ratio"] / red_ref, m["warm_ratio"] / warm_ref))
    gy_ref = max(1e-6, as_float(hc.get("unripe_green_yellow_ratio_reference", 0.08)))
    return min(1.0, m["green_yellow_ratio"] / gy_ref)


def child_weight(d: Detection, model_status: str, cfg: Dict[str, Any]) -> float:
    hc = cfg.get("heuristics", {})
    conf = as_float(d.get("confidence"))
    if model_status == "strong":
        return as_float(hc.get("strong_child_weight", 1.0))
    if conf >= as_float(hc.get("near_threshold_confidence", 0.80)):
        return as_float(hc.get("near_threshold_child_weight", 0.70))
    if conf >= as_float(hc.get("mid_weak_confidence", 0.65)):
        return as_float(hc.get("mid_weak_child_weight", 0.55))
    if conf >= as_float(hc.get("low_weak_confidence", 0.50)):
        return as_float(hc.get("low_weak_child_weight", 0.50))
    return 0.0


def color_correction_info(d: Detection, cfg: Dict[str, Any]) -> Dict[str, Any]:
    """Return color-based class correction info for single tomatoes.

    This is not a YOLO confidence upgrade. It says: YOLO detected a tomato, but
    the segmentation mask color strongly supports the opposite maturity class.
    """
    cc = cfg.get("color_correction", {})
    if not cc.get("enabled", True):
        return {"corrected": False}
    if not is_single_class(d):
        return {"corrected": False}

    class_id = as_int(d.get("class_id"), -1)
    conf = as_float(d.get("confidence"))
    m = metrics_of(d)
    reason = reject_reason(d)

    if conf < as_float(cc.get("min_confidence", 0.50)):
        return {"corrected": False}
    if m["mask_density"] > 0 and m["mask_density"] < as_float(cc.get("min_mask_density", 0.30)):
        return {"corrected": False}
    if m["mask_area"] > 0 and m["mask_area"] < as_float(cc.get("min_mask_area", 250.0)):
        return {"corrected": False}
    if not cc.get("allow_roi_fail", False) and "roi" in reason:
        return {"corrected": False}
    if not cc.get("allow_tomato_sanity_failed", False) and "tomato_sanity_failed" in reason:
        return {"corrected": False}

    # Model says ripe, segmentation color is clearly green/yellow.
    if class_id == 1:
        gy_min = as_float(cc.get("ripe_to_unripe_green_yellow_min", 0.45))
        red_max = as_float(cc.get("ripe_to_unripe_red_max", 0.08))
        warm_max = as_float(cc.get("ripe_to_unripe_warm_max", 0.25))
        if m["green_yellow_ratio"] >= gy_min and m["red_ratio"] <= red_max and m["warm_ratio"] <= warm_max:
            score = m["green_yellow_ratio"] - max(m["red_ratio"], m["warm_ratio"])
            return {
                "corrected": True,
                "original_class_id": 1,
                "original_class_name": class_name_for(1),
                "new_class_id": 2,
                "new_class_name": class_name_for(2),
                "corrected_class_id": 2,
                "corrected_class_name": class_name_for(2),
                "reason": "ripe_to_unripe_by_mask_color",
                "score": score,
            }

    # Model says unripe, segmentation color is clearly red/warm.
    if class_id == 2:
        red_min = as_float(cc.get("unripe_to_ripe_red_min", 0.10))
        warm_min = as_float(cc.get("unripe_to_ripe_warm_min", 0.35))
        gy_max = as_float(cc.get("unripe_to_ripe_green_yellow_max", 0.35))
        if (m["red_ratio"] >= red_min or m["warm_ratio"] >= warm_min) and m["green_yellow_ratio"] <= gy_max:
            score = max(m["red_ratio"], m["warm_ratio"]) - m["green_yellow_ratio"]
            return {
                "corrected": True,
                "original_class_id": 2,
                "original_class_name": class_name_for(2),
                "new_class_id": 1,
                "new_class_name": class_name_for(1),
                "corrected_class_id": 1,
                "corrected_class_name": class_name_for(1),
                "reason": "unripe_to_ripe_by_mask_color",
                "score": score,
            }

    return {"corrected": False}


def review_candidate_info(d: Detection, cfg: Dict[str, Any]) -> Dict[str, Any]:
    """Return near-threshold Review/Candidate info for high-quality weak detections."""
    rcfg = cfg.get("review_candidates", {})
    if not rcfg.get("enabled", True):
        return {"review": False}
    if classify_model_status(d, cfg) != "weak":
        return {"review": False}
    if not is_single_class(d):
        return {"review": False}
    if has_reason(d, "roi") and not rcfg.get("allow_roi_fail", False):
        return {"review": False}
    if has_reason(d, "tomato_sanity_failed") and not rcfg.get("allow_tomato_sanity_failed", False):
        return {"review": False}

    cid = as_int(d.get("class_id"), -1)
    conf = as_float(d.get("confidence"))
    m = metrics_of(d)
    if m["mask_density"] > 0 and m["mask_density"] < as_float(rcfg.get("min_mask_density", 0.35)):
        return {"review": False}

    if cid == 2:
        if (conf >= as_float(rcfg.get("unripe_min_confidence", 0.80))
                and m["green_yellow_ratio"] >= as_float(rcfg.get("unripe_min_green_yellow_ratio", 0.35))
                and (m["mask_area"] <= 0 or m["mask_area"] >= as_float(rcfg.get("unripe_min_mask_area", 1000.0)))):
            return {"review": True, "reason": "near_threshold_unripe_color_mask_support"}
    if cid == 1:
        warm_ok = m["red_ratio"] >= as_float(rcfg.get("ripe_min_red_ratio", 0.08)) or m["warm_ratio"] >= as_float(rcfg.get("ripe_min_warm_ratio", 0.12))
        if (conf >= as_float(rcfg.get("ripe_min_confidence", 0.75))
                and warm_ok
                and (m["mask_area"] <= 0 or m["mask_area"] >= as_float(rcfg.get("ripe_min_mask_area", 500.0)))):
            return {"review": True, "reason": "near_threshold_ripe_color_mask_support"}
    return {"review": False}


def effective_single_class_id(d: Detection, cfg: Dict[str, Any]) -> int:
    ci = color_correction_info(d, cfg)
    if ci.get("corrected"):
        return as_int(ci.get("new_class_id"), as_int(d.get("class_id"), -1))
    return as_int(d.get("class_id"), -1)


def new_policy_info(d: Detection, cfg: Dict[str, Any]) -> Dict[str, Any]:
    old_status = classify_model_status(d, cfg)
    cid = as_int(d.get("class_id"), -1)
    cname = str(d.get("class_name") or class_name_for(cid))

    if str(d.get("source_type")) == "heuristic":
        return {
            "old_status": "none",
            "new_status": "heuristics",
            "new_class_id": cid,
            "new_class_name": cname,
            "reason": str(d.get("promotion_reason", "weak_bunch_anchor_with_child_support")),
        }

    ci = color_correction_info(d, cfg)
    if ci.get("corrected"):
        return {
            "old_status": old_status,
            "new_status": "color_corrected",
            "new_class_id": as_int(ci.get("new_class_id"), cid),
            "new_class_name": str(ci.get("new_class_name")),
            "reason": str(ci.get("reason")),
            "color_correction": ci,
        }

    ri = review_candidate_info(d, cfg)
    if ri.get("review"):
        return {
            "old_status": old_status,
            "new_status": "review",
            "new_class_id": cid,
            "new_class_name": cname,
            "reason": str(ri.get("reason")),
            "review": ri,
        }

    return {
        "old_status": old_status,
        "new_status": old_status,
        "new_class_id": cid,
        "new_class_name": cname,
        "reason": "",
    }


def anchor_tier(anchor_conf: float, cfg: Dict[str, Any]) -> Optional[Dict[str, Any]]:
    hc = cfg.get("heuristics", {})
    tiers = hc.get("anchor_confidence_tiers")
    if not isinstance(tiers, list) or not tiers:
        min_conf = as_float(hc.get("anchor_min_confidence", 0.50))
        if anchor_conf >= min_conf:
            return {
                "min_confidence": min_conf,
                "min_child_count": as_int(hc.get("min_child_count", 3)),
                "min_weighted_child_count": as_float(hc.get("min_weighted_child_count", 1.5)),
            }
        return None
    usable = []
    for t in tiers:
        if not isinstance(t, dict):
            continue
        usable.append(t)
    usable.sort(key=lambda x: as_float(x.get("min_confidence", 0.0)), reverse=True)
    for t in usable:
        if anchor_conf >= as_float(t.get("min_confidence", 0.0)):
            return t
    return None


def eligible_anchor(d: Detection, cfg: Dict[str, Any]) -> bool:
    hc = cfg.get("heuristics", {})
    if as_int(d.get("class_id"), -1) not in set(hc.get("anchor_classes", [0, 3])):
        return False
    if classify_model_status(d, cfg) == "strong":
        return False
    if anchor_tier(as_float(d.get("confidence")), cfg) is None:
        return False
    reason = reject_reason(d)
    if not hc.get("anchor_allow_roi_fail", False) and "roi" in reason:
        return False
    # Important v3 change: a weak bunch anchor is allowed even with low bunch
    # mask density, because the children can be the stronger evidence. Extremely
    # low confidence is still blocked by anchor_confidence_tiers.
    min_density = as_float(hc.get("anchor_hard_min_mask_density", 0.0))
    m = metrics_of(d)
    if min_density > 0.0 and m["mask_density"] > 0.0 and m["mask_density"] < min_density:
        return False
    return True


def eligible_child(d: Detection, cfg: Dict[str, Any]) -> bool:
    hc = cfg.get("heuristics", {})
    if as_int(d.get("class_id"), -1) not in set(hc.get("child_classes", [1, 2])):
        return False
    if classify_model_status(d, cfg) == "noise":
        return False
    if as_float(d.get("confidence")) < as_float(hc.get("child_min_confidence", 0.50)):
        return False
    reason = reject_reason(d)
    if not hc.get("child_allow_roi_fail", False) and "roi" in reason:
        return False
    if not hc.get("child_allow_tomato_sanity_failed", False) and "tomato_sanity_failed" in reason:
        return False
    m = metrics_of(d)
    if m["mask_density"] > 0.0 and m["mask_density"] < as_float(hc.get("child_min_mask_density", 0.08)):
        return False
    if m["mask_area"] > 0.0 and m["mask_area"] < as_float(hc.get("child_min_mask_area", 100.0)):
        return False
    return True


def frame_key(d: Detection) -> Tuple[str, int, str]:
    return (str(d.get("image_path", "")), as_int(d.get("frame_id"), 0), str(d.get("image_group", "")))


def image_dims_from_cfg(cfg: Dict[str, Any]) -> Tuple[Optional[int], Optional[int]]:
    w = cfg.get("image_width")
    h = cfg.get("image_height")
    iw = as_int(w, 0) if w is not None else 0
    ih = as_int(h, 0) if h is not None else 0
    return (iw or None, ih or None)


def create_heuristics_for_frame(rows: List[Detection], cfg: Dict[str, Any]) -> List[Detection]:
    hc = cfg.get("heuristics", {})
    if not hc.get("enabled", True):
        return []

    anchors = [d for d in rows if eligible_anchor(d, cfg)]
    children_all = [d for d in rows if eligible_child(d, cfg)]
    candidates: List[Detection] = []

    image_w, image_h = image_dims_from_cfg(cfg)

    for anchor in anchors:
        tier = anchor_tier(as_float(anchor.get("confidence")), cfg)
        if tier is None:
            continue
        tier_min_child_count = as_int(tier.get("min_child_count", hc.get("min_child_count", 3)))
        tier_min_weighted = as_float(tier.get("min_weighted_child_count", hc.get("min_weighted_child_count", 1.5)))

        anchor_box = bbox_of(anchor)
        search_box = expand_bbox(anchor_box, as_float(hc.get("anchor_expand_ratio", 0.20)), image_w, image_h)
        children = [c for c in children_all if center_inside(c, search_box)]
        if len(children) < tier_min_child_count:
            continue

        weighted_count = 0.0
        ripe_score = 0.0
        unripe_score = 0.0
        weak_child_count = 0
        strong_child_count = 0
        color_corrected_child_count = 0
        child_ids: List[str] = []

        for c in children:
            st = classify_model_status(c, cfg)
            if st == "noise":
                continue
            if st == "strong":
                strong_child_count += 1
            else:
                weak_child_count += 1
            w = child_weight(c, st, cfg)
            if w <= 0.0:
                continue
            weighted_count += w
            child_ids.append(str(c.get("detection_id")))

            ripe_color = color_support_for_child(c, "ripe", cfg)
            unripe_color = color_support_for_child(c, "unripe", cfg)
            eff_id = effective_single_class_id(c, cfg)
            if eff_id != as_int(c.get("class_id"), -1):
                color_corrected_child_count += 1

            if eff_id == 1:
                ripe_score += w * (0.75 + 0.25 * ripe_color)
                if unripe_color > 0.85 and ripe_color < 0.60:
                    unripe_score += w * 0.30
            elif eff_id == 2:
                unripe_score += w * (0.75 + 0.25 * unripe_color)
                if ripe_color > 0.85 and unripe_color < 0.60:
                    ripe_score += w * 0.30

        if weighted_count < tier_min_weighted:
            continue

        majority = as_float(hc.get("maturity_majority_ratio", 1.25))
        min_score = as_float(hc.get("min_maturity_score_for_decision", 1.25))
        allow_mixed = bool(hc.get("allow_mixed_bunch", True))

        dominant = "mixed"
        class_id = 104
        class_name = "HEURISTIC mixed bunch"
        if ripe_score >= min_score and ripe_score >= unripe_score * majority:
            dominant = "ripe"
            class_id = 100
            class_name = "HEURISTIC ripe bunch"
        elif unripe_score >= min_score and unripe_score >= ripe_score * majority:
            dominant = "unripe"
            class_id = 103
            class_name = "HEURISTIC unripe bunch"
        elif not allow_mixed:
            continue

        boxes = [anchor_box] + [bbox_of(c) for c in children]
        out_box = union_bboxes(boxes, as_float(hc.get("bbox_padding_ratio", 0.08)), image_w, image_h)
        score = weighted_count + max(ripe_score, unripe_score) * 0.35 + as_float(anchor.get("confidence"))

        heuristic: Detection = {
            "schema": "rbv2_policy_replay_python_heuristic_v3",
            "session_id": anchor.get("session_id"),
            "image_group": anchor.get("image_group"),
            "image_path": anchor.get("image_path"),
            "image_abs_path": anchor.get("image_abs_path"),
            "frame_id": anchor.get("frame_id"),
            "detection_id": f"h_{anchor.get('detection_id')}",
            "source_type": "heuristic",
            "source_kind": "weak_bunch_anchor_heuristic",
            "class_id": class_id,
            "class_name": class_name,
            "confidence": None,
            "old_status": "none",
            "new_status": "heuristics",
            "old_reject_reason": "",
            "new_reject_reason": "",
            "promoted_by_policy": True,
            "promotion_reason": "weak_bunch_anchor_with_child_support_v3",
            "heuristic_type": "weak_bunch_anchor",
            "anchor_detection_id": anchor.get("detection_id"),
            "anchor_bunch_class": anchor.get("class_name"),
            "anchor_bunch_confidence": as_float(anchor.get("confidence")),
            "anchor_mask_density": metrics_of(anchor)["mask_density"],
            "anchor_tier_min_child_count": tier_min_child_count,
            "anchor_tier_min_weighted_child_count": tier_min_weighted,
            "child_detection_ids": child_ids,
            "child_count": len(children),
            "weak_child_count": weak_child_count,
            "strong_child_count": strong_child_count,
            "color_corrected_child_count": color_corrected_child_count,
            "weighted_child_count": weighted_count,
            "ripe_evidence_score": ripe_score,
            "unripe_evidence_score": unripe_score,
            "dominant_maturity": dominant,
            "heuristic_score": score,
            "bbox_source": "union_anchor_children",
            "bbox": out_box,
            "bbox_x": out_box["x"],
            "bbox_y": out_box["y"],
            "bbox_w": out_box["w"],
            "bbox_h": out_box["h"],
            "metrics": {
                "box_area": out_box["w"] * out_box["h"],
                "mask_area": 0.0,
                "mask_density": 0.0,
                "red_ratio": 0.0,
                "orange_ratio": 0.0,
                "warm_ratio": 0.0,
                "green_yellow_ratio": 0.0,
            },
        }
        candidates.append(heuristic)

    # Heuristic NMS: avoid many yellow boxes on the same weak bunch region.
    candidates.sort(key=lambda d: as_float(d.get("heuristic_score")), reverse=True)
    kept: List[Detection] = []
    nms_thr = as_float(hc.get("heuristic_nms_iou", 0.35))
    for c in candidates:
        cb = bbox_of(c)
        if any(bbox_iou(cb, bbox_of(k)) > nms_thr for k in kept):
            continue
        c["detection_id"] = f"heuristic_{len(kept)}"
        kept.append(c)
    return kept


def build_csv_row(d: Detection, cfg: Dict[str, Any], source_type: str) -> Dict[str, Any]:
    m = metrics_of(d)
    b = bbox_of(d)
    info = new_policy_info(d, cfg) if source_type == "model" else new_policy_info(dict(d, source_type="heuristic"), cfg)
    old_status = info.get("old_status", "none" if source_type == "heuristic" else classify_model_status(d, cfg))
    new_status = info.get("new_status", old_status)
    promoted = source_type == "heuristic"

    original_class_id = as_int(d.get("class_id"), -1)
    original_class_name = str(d.get("class_name") or class_name_for(original_class_id))
    new_class_id = as_int(info.get("new_class_id"), original_class_id)
    new_class_name = str(info.get("new_class_name") or class_name_for(new_class_id, original_class_name))

    ci = info.get("color_correction") if isinstance(info.get("color_correction"), dict) else {}
    ri = info.get("review") if isinstance(info.get("review"), dict) else {}

    return {
        "session_id": d.get("session_id", ""),
        "image_path": d.get("image_path", ""),
        "image_group": d.get("image_group", ""),
        "frame_id": d.get("frame_id", ""),
        "detection_id": d.get("detection_id", ""),
        "source_type": source_type,
        "class_id": d.get("class_id", ""),
        "class_name": d.get("class_name", ""),
        "confidence": "" if d.get("confidence") is None else d.get("confidence", ""),
        "old_status": old_status,
        "new_status": new_status,
        "old_reject_reason": d.get("old_reject_reason", ""),
        "new_reject_reason": info.get("reason", d.get("new_reject_reason", "")),
        "original_class_id": original_class_id,
        "original_class_name": original_class_name,
        "new_class_id": new_class_id,
        "new_class_name": new_class_name,
        "corrected_class_id": ci.get("corrected_class_id", ""),
        "corrected_class_name": ci.get("corrected_class_name", ""),
        "class_corrected_by_color": bool(ci.get("corrected", False)),
        "correction_reason": ci.get("reason", ""),
        "color_correction_score": ci.get("score", ""),
        "review_candidate": bool(ri.get("review", False)),
        "review_reason": ri.get("reason", ""),
        "mask_density": m["mask_density"],
        "mask_area": m["mask_area"],
        "box_area": m["box_area"],
        "red_ratio": m["red_ratio"],
        "orange_ratio": m["orange_ratio"],
        "warm_ratio": m["warm_ratio"],
        "green_yellow_ratio": m["green_yellow_ratio"],
        "bbox_x": b["x"],
        "bbox_y": b["y"],
        "bbox_w": b["w"],
        "bbox_h": b["h"],
        "promoted_by_policy": promoted,
        "promotion_reason": d.get("promotion_reason", ""),
        "heuristic_type": d.get("heuristic_type", ""),
        "anchor_detection_id": d.get("anchor_detection_id", ""),
        "anchor_bunch_class": d.get("anchor_bunch_class", ""),
        "anchor_bunch_confidence": d.get("anchor_bunch_confidence", ""),
        "child_detection_ids": ";".join(map(str, d.get("child_detection_ids", []))) if isinstance(d.get("child_detection_ids"), list) else d.get("child_detection_ids", ""),
        "child_count": d.get("child_count", ""),
        "weak_child_count": d.get("weak_child_count", ""),
        "strong_child_count": d.get("strong_child_count", ""),
        "weighted_child_count": d.get("weighted_child_count", ""),
        "ripe_evidence_score": d.get("ripe_evidence_score", ""),
        "unripe_evidence_score": d.get("unripe_evidence_score", ""),
        "dominant_maturity": d.get("dominant_maturity", ""),
        "heuristic_score": d.get("heuristic_score", ""),
        "bbox_source": d.get("bbox_source", ""),
    }


def get_colors(cfg: Dict[str, Any]) -> Dict[str, Tuple[int, int, int]]:
    raw = cfg.get("display_colors_bgr", {})

    def tup(name: str, default: Tuple[int, int, int]) -> Tuple[int, int, int]:
        v = raw.get(name)
        if isinstance(v, list) and len(v) == 3:
            return (int(v[0]), int(v[1]), int(v[2]))
        return default

    return {
        "strong_ripe_tomato": tup("strong_ripe_tomato", (0, 0, 255)),
        "strong_unripe_tomato": tup("strong_unripe_tomato", (0, 180, 0)),
        "strong_ripe_bunch": tup("strong_ripe_bunch", (180, 0, 180)),
        "strong_unripe_bunch": tup("strong_unripe_bunch", (0, 165, 255)),
        "weak": tup("weak", (255, 255, 0)),
        "noise": tup("noise", (145, 145, 145)),
        "heuristics": tup("heuristics", (0, 255, 255)),
        "review": tup("review", (0, 215, 255)),
        "text_bg": tup("text_bg", (20, 24, 34)),
    }


def class_color(class_id: int, colors: Dict[str, Tuple[int, int, int]]) -> Tuple[int, int, int]:
    if class_id == 0:
        return colors["strong_ripe_bunch"]
    if class_id == 1:
        return colors["strong_ripe_tomato"]
    if class_id == 2:
        return colors["strong_unripe_tomato"]
    if class_id == 3:
        return colors["strong_unripe_bunch"]
    return colors["heuristics"]


def draw_label(img: Any, text: str, x: int, y: int, color: Tuple[int, int, int]) -> None:
    if cv2 is None:
        return
    font = cv2.FONT_HERSHEY_SIMPLEX
    scale = 0.55
    thickness = 2
    (tw, th), baseline = cv2.getTextSize(text, font, scale, thickness)
    x = max(0, min(img.shape[1] - 1, x))
    y_top = max(0, y - th - baseline - 4)
    x_right = min(img.shape[1] - 1, x + tw + 6)
    cv2.rectangle(img, (x, y_top), (x_right, y_top + th + baseline + 6), color, -1)
    cv2.putText(img, text, (x + 3, y_top + th + 1), font, scale, (0, 0, 0), thickness, cv2.LINE_AA)


def draw_detection(img: Any, d: Detection, cfg: Dict[str, Any], mode: str) -> None:
    if cv2 is None:
        return
    colors = get_colors(cfg)
    b = bbox_of(d)
    x1 = int(round(b["x"]))
    y1 = int(round(b["y"]))
    x2 = int(round(b["x"] + b["w"]))
    y2 = int(round(b["y"] + b["h"]))
    x1 = max(0, min(img.shape[1] - 1, x1))
    y1 = max(0, min(img.shape[0] - 1, y1))
    x2 = max(0, min(img.shape[1] - 1, x2))
    y2 = max(0, min(img.shape[0] - 1, y2))

    if str(d.get("source_type")) == "heuristic":
        color = colors["heuristics"]
        text = str(d.get("class_name", "HEURISTIC"))
        support = d.get("child_count")
        if support != "":
            text += f" support={support}"
        cv2.rectangle(img, (x1, y1), (x2, y2), color, 4)
        draw_label(img, text, x1, max(18, y1), color)
        return

    old_status = classify_model_status(d, cfg)
    cid = as_int(d.get("class_id"), -1)
    conf_pct = int(round(as_float(d.get("confidence")) * 100))

    if mode == "old":
        if old_status == "strong":
            color = class_color(cid, colors)
        elif old_status == "noise":
            color = colors["noise"]
        else:
            color = colors["weak"]
        text = f"{d.get('class_name', '')} {conf_pct}% {old_status}"
        cv2.rectangle(img, (x1, y1), (x2, y2), color, 2)
        draw_label(img, text, x1, max(18, y1), color)
        return

    info = new_policy_info(d, cfg)
    new_status = str(info.get("new_status", old_status))
    new_cid = as_int(info.get("new_class_id"), cid)
    new_cname = str(info.get("new_class_name") or d.get("class_name", ""))

    if new_status == "strong":
        color = class_color(new_cid, colors)
        text = f"{new_cname} {conf_pct}% strong"
        thick = 2
    elif new_status == "color_corrected":
        color = class_color(new_cid, colors)
        old_name = str(d.get("class_name", ""))
        text = f"CORR {old_name}->{new_cname} {conf_pct}%"
        thick = 3
    elif new_status == "review":
        # Review is not Strong. It is drawn with the class color but explicitly
        # labelled REVIEW so it can be checked visually.
        color = class_color(new_cid, colors)
        text = f"REVIEW {new_cname} {conf_pct}%"
        thick = 3
    elif new_status == "noise":
        color = colors["noise"]
        text = f"{d.get('class_name', '')} {conf_pct}% noise"
        thick = 2
    else:
        color = colors["weak"]
        text = f"{d.get('class_name', '')} {conf_pct}% weak"
        thick = 2

    cv2.rectangle(img, (x1, y1), (x2, y2), color, thick)
    draw_label(img, text, x1, max(18, y1), color)


def add_header(img: Any, title: str, color: Tuple[int, int, int]) -> Any:
    """Return a copy of img with a small title strip at the top."""
    if cv2 is None:
        return img
    pad_h = 42
    out = cv2.copyMakeBorder(img, pad_h, 0, 0, 0, cv2.BORDER_CONSTANT, value=(20, 24, 34))
    cv2.putText(out, title, (12, 28), cv2.FONT_HERSHEY_SIMPLEX, 0.78, color, 2, cv2.LINE_AA)
    return out


def draw_debug_images(rows_by_frame: Dict[Tuple[str, int, str], List[Detection]],
                      heur_by_frame: Dict[Tuple[str, int, str], List[Detection]],
                      out_dir: Path,
                      cfg: Dict[str, Any],
                      max_debug_images: int = 0) -> List[Dict[str, Any]]:
    """Draw visual debug outputs for the notebook/research workflow."""
    if cv2 is None:
        raise RuntimeError(f"cv2 import failed: {CV2_IMPORT_ERROR}")

    old_dir = out_dir / "old_policy_debug"
    new_dir = out_dir / "new_policy_debug"
    heur_dir = out_dir / "heuristics_only_debug"
    cmp_dir = out_dir / "comparison_debug"
    for d in (old_dir, new_dir, heur_dir, cmp_dir):
        d.mkdir(parents=True, exist_ok=True)

    debug_index: List[Dict[str, Any]] = []
    colors = get_colors(cfg)

    count = 0
    for key, rows in sorted(rows_by_frame.items(), key=lambda kv: (kv[0][1], kv[0][0])):
        if max_debug_images and count >= max_debug_images:
            break
        if not rows:
            continue
        img_path = Path(str(rows[0].get("image_abs_path") or rows[0].get("image_path")))
        img = cv2.imread(str(img_path), cv2.IMREAD_COLOR)
        if img is None:
            continue

        heuristics = heur_by_frame.get(key, [])
        old_img = img.copy()
        for d in rows:
            dd = dict(d)
            dd["source_type"] = "model"
            draw_detection(old_img, dd, cfg, "old")

        new_img = img.copy()
        for d in rows:
            dd = dict(d)
            dd["source_type"] = "model"
            draw_detection(new_img, dd, cfg, "new")
        for h in heuristics:
            draw_detection(new_img, h, cfg, "new")

        heur_img = img.copy()
        for h in heuristics:
            draw_detection(heur_img, h, cfg, "heuristics_only")

        safe_name = f"frame_{key[1]:06d}_{Path(key[0]).stem}.jpg"
        old_path = old_dir / safe_name
        new_path = new_dir / safe_name
        heur_path = heur_dir / safe_name
        cmp_path = cmp_dir / safe_name

        cv2.imwrite(str(old_path), old_img)
        cv2.imwrite(str(new_path), new_img)
        cv2.imwrite(str(heur_path), heur_img)

        old_header = add_header(old_img, "OLD POLICY: model Strong / Weak / Noise", (220, 220, 220))
        new_header = add_header(new_img, "NEW POLICY: Review + Color Correction + YELLOW Heuristics", colors["heuristics"])
        if old_header.shape[0] == new_header.shape[0]:
            cmp_img = cv2.hconcat([old_header, new_header])
        else:
            h = min(old_header.shape[0], new_header.shape[0])
            cmp_img = cv2.hconcat([old_header[:h], new_header[:h]])
        cv2.imwrite(str(cmp_path), cmp_img)

        old_counts = Counter(classify_model_status(d, cfg) for d in rows)
        new_counts = Counter(str(new_policy_info(d, cfg).get("new_status")) for d in rows)
        debug_index.append({
            "image_path": key[0],
            "image_group": key[2],
            "frame_id": key[1],
            "model_detection_count": len(rows),
            "model_strong_count": old_counts.get("strong", 0),
            "model_weak_count": old_counts.get("weak", 0),
            "model_noise_count": old_counts.get("noise", 0),
            "new_strong_count": new_counts.get("strong", 0),
            "new_review_count": new_counts.get("review", 0),
            "new_color_corrected_count": new_counts.get("color_corrected", 0),
            "new_weak_count": new_counts.get("weak", 0),
            "new_noise_count": new_counts.get("noise", 0),
            "heuristics_count": len(heuristics),
            "old_policy_debug_path": str(old_path),
            "new_policy_debug_path": str(new_path),
            "heuristics_only_debug_path": str(heur_path),
            "comparison_debug_path": str(cmp_path),
        })
        count += 1

    index_path = out_dir / "debug_image_index.csv"
    fields = [
        "image_path", "image_group", "frame_id",
        "model_detection_count", "model_strong_count", "model_weak_count", "model_noise_count",
        "new_strong_count", "new_review_count", "new_color_corrected_count", "new_weak_count", "new_noise_count",
        "heuristics_count",
        "old_policy_debug_path", "new_policy_debug_path", "heuristics_only_debug_path", "comparison_debug_path",
    ]
    write_csv(index_path, debug_index, fields)
    return debug_index


def run_analysis(cpp_jsonl: Path,
                 config_path: Path,
                 output_dir: Path,
                 max_debug_images: int = 0) -> Dict[str, Any]:
    cfg = load_json(config_path)
    rows = load_jsonl(cpp_jsonl)
    output_dir.mkdir(parents=True, exist_ok=True)

    rows_by_frame: Dict[Tuple[str, int, str], List[Detection]] = defaultdict(list)
    for d in rows:
        rows_by_frame[frame_key(d)].append(d)

    heuristics: List[Detection] = []
    heur_by_frame: Dict[Tuple[str, int, str], List[Detection]] = defaultdict(list)
    for key, frame_rows in rows_by_frame.items():
        hs = create_heuristics_for_frame(frame_rows, cfg)
        heur_by_frame[key].extend(hs)
        heuristics.extend(hs)

    csv_rows: List[Dict[str, Any]] = []
    for d in rows:
        csv_rows.append(build_csv_row(d, cfg, "model"))
    for h in heuristics:
        csv_rows.append(build_csv_row(h, cfg, "heuristic"))

    csv_path = output_dir / "policy_compare.csv"
    write_csv(csv_path, csv_rows, REQUESTED_CSV_FIELDS)

    heur_jsonl = output_dir / "heuristics.jsonl"
    write_jsonl(heur_jsonl, heuristics)

    color_correction_rows = [r for r in csv_rows if r.get("source_type") == "model" and str(r.get("new_status")) == "color_corrected"]
    review_rows = [r for r in csv_rows if r.get("source_type") == "model" and str(r.get("new_status")) == "review"]
    manual_review_rows = [r for r in csv_rows if str(r.get("new_status")) in ("review", "color_corrected", "heuristics")]

    write_csv(output_dir / "color_corrections.csv", color_correction_rows, REQUESTED_CSV_FIELDS)
    write_csv(output_dir / "regular_review_candidates.csv", review_rows, REQUESTED_CSV_FIELDS)
    write_csv(output_dir / "manual_review_queue.csv", manual_review_rows, REQUESTED_CSV_FIELDS)
    write_jsonl(output_dir / "color_corrections.jsonl", [r for r in rows if new_policy_info(r, cfg).get("new_status") == "color_corrected"])
    write_jsonl(output_dir / "review_candidates.jsonl", [r for r in rows if new_policy_info(r, cfg).get("new_status") == "review"])

    debug_index = draw_debug_images(rows_by_frame, heur_by_frame, output_dir, cfg, max_debug_images=max_debug_images)

    model_old_status_counts = Counter(build_csv_row(d, cfg, "model")["old_status"] for d in rows)
    model_new_status_counts = Counter(build_csv_row(d, cfg, "model")["new_status"] for d in rows)
    heuristic_class_counts = Counter(str(h.get("class_name")) for h in heuristics)
    heuristic_maturity_counts = Counter(str(h.get("dominant_maturity")) for h in heuristics)
    by_group = Counter(str(d.get("image_group")) for d in rows)
    heur_by_group = Counter(str(h.get("image_group")) for h in heuristics)
    color_corr_by_reason = Counter(str(r.get("correction_reason")) for r in color_correction_rows)
    review_by_reason = Counter(str(r.get("review_reason")) for r in review_rows)

    summary = {
        "schema": "rbv2_policy_replay_python_summary_v3",
        "cpp_jsonl": str(cpp_jsonl),
        "config_path": str(config_path),
        "output_dir": str(output_dir),
        "model_rows": len(rows),
        "frames": len(rows_by_frame),
        "model_old_status_counts": dict(model_old_status_counts),
        "model_new_status_counts": dict(model_new_status_counts),
        "heuristics_total": len(heuristics),
        "heuristic_class_counts": dict(heuristic_class_counts),
        "heuristic_maturity_counts": dict(heuristic_maturity_counts),
        "color_corrections_total": len(color_correction_rows),
        "color_corrections_by_reason": dict(color_corr_by_reason),
        "review_candidates_total": len(review_rows),
        "review_candidates_by_reason": dict(review_by_reason),
        "manual_review_queue_total": len(manual_review_rows),
        "model_rows_by_group": dict(by_group),
        "heuristics_by_group": dict(heur_by_group),
        "csv_path": str(csv_path),
        "heuristics_jsonl": str(heur_jsonl),
        "color_corrections_csv": str(output_dir / "color_corrections.csv"),
        "regular_review_candidates_csv": str(output_dir / "regular_review_candidates.csv"),
        "manual_review_queue_csv": str(output_dir / "manual_review_queue.csv"),
        "old_policy_debug_dir": str(output_dir / "old_policy_debug"),
        "new_policy_debug_dir": str(output_dir / "new_policy_debug"),
        "heuristics_only_debug_dir": str(output_dir / "heuristics_only_debug"),
        "comparison_debug_dir": str(output_dir / "comparison_debug"),
        "debug_image_index_csv": str(output_dir / "debug_image_index.csv"),
        "debug_images_written": len(debug_index),
        "debug_images_with_heuristics": sum(1 for r in debug_index if as_int(r.get("heuristics_count"), 0) > 0),
        "debug_images_with_color_correction": sum(1 for r in debug_index if as_int(r.get("new_color_corrected_count"), 0) > 0),
        "debug_images_with_review": sum(1 for r in debug_index if as_int(r.get("new_review_count"), 0) > 0),
    }
    with (output_dir / "summary.json").open("w", encoding="utf-8") as f:
        json.dump(summary, f, ensure_ascii=False, indent=2, sort_keys=True)
    return summary


def main() -> int:
    ap = argparse.ArgumentParser(description="Analyze RBV2 C++ replay JSONL and add ver32 experimental policy layers.")
    ap.add_argument("--cpp-jsonl", required=True, type=Path, help="cpp_replay_detections.jsonl")
    ap.add_argument("--config", default=Path("policy_replay_lab/configs/policy_v32_experiment.json"), type=Path)
    ap.add_argument("--output", required=True, type=Path, help="analysis output folder")
    ap.add_argument("--max-debug-images", default=0, type=int, help="0 means draw all images")
    args = ap.parse_args()

    summary = run_analysis(args.cpp_jsonl, args.config, args.output, max_debug_images=args.max_debug_images)
    print(json.dumps(summary, ensure_ascii=False, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
