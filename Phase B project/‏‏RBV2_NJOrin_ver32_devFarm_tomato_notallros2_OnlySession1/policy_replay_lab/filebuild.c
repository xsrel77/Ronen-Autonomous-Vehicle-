

chmod +x policy_replay_lab/scripts/*.sh
ls -R policy_replay_lab | head -50
ls -lh models/best8s_seg_v43_fp16.engine


ls Debugging/toClient/sessions/session_20260630_103628
ls Debugging/toClient/sessions/session_20260630_103628/images_ok_raw | head
ls Debugging/toClient/sessions/session_20260630_103628/images_weak_noise_raw | head

-------------------
קומפילציה של כלי ה־C++ Replay 
./policy_replay_lab/scripts/build_policy_replay_cli.sh
הרצת ניסיון קצר 
./policy_replay_lab/scripts/run_policy_replay_example.sh Debugging/toClient/sessions/session_20260630_103628 policy_only 10


הרצת ניסיון רגיל 
./policy_replay_lab/scripts/run_policy_replay_example.sh Debugging/toClient/sessions/session_20260630_103628 

