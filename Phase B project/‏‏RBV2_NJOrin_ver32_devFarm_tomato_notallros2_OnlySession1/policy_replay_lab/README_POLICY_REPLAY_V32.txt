RBV2 ver32 - Policy Replay Lab PATCH v3
========================================

Purpose
-------
This patch adds an isolated research folder named:

  policy_replay_lab/

It is not inside Debugging/. The normal robot runtime is not supposed to depend
on this folder. The goal is to replay greenhouse session images with the real
TensorRT engine, export C++ detections, and then compare the old policy against
an experimental policy layer in Python.

Patch v3 changes compared to v2
-------------------------------
1. Visual colors were corrected:
   - Strong ripe tomato       = red
   - Strong unripe tomato     = green
   - Strong ripe bunch        = purple
   - Strong unripe bunch      = orange
   - Weak                     = cyan
   - Noise                    = gray
   - Heuristics               = yellow

2. Weak Bunch Anchor Heuristics were improved:
   - A weak bunch anchor is no longer rejected only because bunch mask density is low.
   - The weak bunch can be used as a spatial anchor if child tomato detections support it.
   - Default tiers:
       anchor >= 0.50  + at least 3 children + weighted_child_count >= 1.5
       anchor >= 0.40  + at least 4 children + weighted_child_count >= 2.5
   - Heuristics can output:
       HEURISTIC ripe bunch
       HEURISTIC unripe bunch
       HEURISTIC mixed bunch
   - Heuristics are always drawn in yellow and are not YOLO confidence upgrades.

3. Color-based Class Correction was added for single tomatoes:
   - If YOLO predicts ripe tomato but the segmentation mask is clearly green/yellow,
     the new policy marks it as color_corrected to unripe.
   - If YOLO predicts unripe tomato but the segmentation mask is clearly red/warm,
     the new policy marks it as color_corrected to ripe.
   - This is visible in debug images as labels like:
       CORR ripe->unripe 78%
       CORR unripe->ripe 82%
   - This does not retrain the model. It is a post-policy research layer.

4. Review/Candidate detections were added:
   - High-quality near-threshold weak tomatoes can become Review instead of remaining plain Weak.
   - Example default rule for unripe:
       confidence >= 0.80, green_yellow_ratio >= 0.35, mask_density >= 0.35
   - Example default rule for ripe:
       confidence >= 0.75, red_ratio/warm_ratio support, mask_density >= 0.35
   - Review is not Strong. It is for visual research and manual checking.

5. New output files:
   policy_compare.csv
   heuristics.jsonl
   color_corrections.csv
   color_corrections.jsonl
   regular_review_candidates.csv
   review_candidates.jsonl
   manual_review_queue.csv
   summary.json
   debug_image_index.csv
   old_policy_debug/
   new_policy_debug/
   heuristics_only_debug/
   comparison_debug/

Main workflow
-------------
1. C++ CLI uses the real ObjectDetector and TensorRT engine.
2. It exports detections to policy_replay_lab/outputs/<session>/cpp_replay/.
3. Python analysis reads the C++ output and adds:
   - Review/Candidate
   - Color Correction
   - Yellow Heuristics bunch candidates
4. Images are saved for visual comparison.

Important
---------
This patch does not train anything, does not use Roboflow, and does not convert
models. It uses the existing TensorRT engine, for example:

  models/best8s_seg_v43_fp16.engine

Recommended first run
---------------------
Run on 10 images first:

  cd ~/Desktop/RBV2_NJOrin_ver32_devFarm_tomato
  ./policy_replay_lab/scripts/run_policy_replay_example.sh Debugging/toClient/sessions/session_20260630_103628 policy_only 10

Then open:

  policy_replay_lab/outputs/session_20260630_103628/analysis/comparison_debug/
  policy_replay_lab/outputs/session_20260630_103628/analysis/heuristics_only_debug/

Full session run:

  ./policy_replay_lab/scripts/run_policy_replay_example.sh Debugging/toClient/sessions/session_20260630_103628 policy_only 0

Jetson memory note
------------------
On Jetson Orin Nano 8GB, close VS Code, browser, Jupyter, and the normal robot
runtime before running replay. TensorRT loads the engine before processing any
image, so max_images=10 does not reduce the engine memory requirement.
