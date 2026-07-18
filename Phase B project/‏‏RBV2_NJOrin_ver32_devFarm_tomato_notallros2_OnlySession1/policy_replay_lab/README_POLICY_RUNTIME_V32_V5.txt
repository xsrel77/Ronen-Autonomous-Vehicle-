RBV2 ver32 - Policy Runtime PATCH v5
====================================

Purpose
-------
Fix the live GUI/L1 policy runtime after PATCH v4.

The v4 patch exposed an important bug:
- class_id=4 / "heuristic mixed bunch" was accidentally added to cfg.classNames.
- cfg.classNames.size() was used by YOLOv8-seg postprocess as the number of MODEL classes.
- The TensorRT engine actually has 4 model classes only:
  0 eripe bunch, 1 ripe, 2 unripe, 3 unripe bunch.
- By making the postprocess think there are 5 classes, the first mask coefficient was read as a fake class score.
- Result: thousands of false "heuristic mixed bunch" MODEL candidates, noisy boxes, and broken/shifted masks.

What v5 changes
---------------
1) Separates model classes from heuristic-only labels:
   - Adds Config::modelClassCount = 4.
   - YOLO decoding now uses modelClassCount, not classNames.size().
   - classNames returns only the four real model classes.
   - Heuristic labels are still exported manually as:
     HEURISTIC ripe bunch / HEURISTIC unripe bunch / HEURISTIC mixed bunch.

2) Restores segmentation masks:
   - Because mask coefficients are no longer shifted, single tomato masks should appear again.
   - CAMERA GUI keeps:
     ripe tomato mask = red
     unripe tomato mask = green

3) Tightens live Heuristics to reduce noisy yellow boxes:
   - Runtime anchor min confidence: 0.55 instead of 0.40.
   - Strong anchor threshold: 0.70.
   - Child min confidence: 0.55.
   - Strong anchor requires at least 4 child tomatoes and weighted >= 2.6.
   - Weak anchor requires at least 5 child tomatoes and weighted >= 3.4.
   - Anchor padding reduced from 0.18 to 0.10.
   - Union padding reduced from 0.08 to 0.04.
   - Duplicate IoU lowered to 0.30.
   - Max heuristics per frame = 2.
   - Max heuristic box area = 18% of frame.
   - Weak bunch anchor must have at least minimal mask support:
     mask density >= 0.02 and mask area >= 300 when a mask exists.

4) Next.js / L1 compatibility
-----------------------------
This patch is still additive.
Existing fields remain:
- label
- class_id
- confidence
- weak
- bbox

New fields from v4 remain available:
- source_type
- policy_status
- heuristic
- review_candidate
- color_corrected_by_policy
- heuristic_meta
- heuristic_count
- review_count
- color_corrected_count
- noise_count

Important expected result after v5
----------------------------------
In raw_candidates.jsonl you should NOT see thousands of class_id=4 model rows.
A healthy quick check:
  grep -c '"class_id":4' latest raw_candidates should be near zero for raw model candidates.
  class_id=4 should appear only for true HEURISTIC mixed bunch rows in final detections.

Build
-----
After unzip, rebuild amain:
  ./scripts/build_main_ver32_policy_runtime.sh

Then run:
  sudo ./amain
