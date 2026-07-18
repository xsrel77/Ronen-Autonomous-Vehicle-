עדכון מיפוי כפתורים במצב j - Joystick + GUI
==============================================

מטרה:
להעביר פעולות מכפתורי ה-Joystick אל המקלדת, כדי לפנות כפתורי Joystick לפיצ'רים חדשים.

קבצים שעודכנו:
1. orchestration/JoystickController.cpp
2. app/amain.cpp

מיפוי Joystick שנשאר במצב j:
-----------------------------
Left Stick Y   - נסיעה קדימה / אחורה
Right Stick X  - היגוי שמאלה / ימינה
A              - הפעלה / כיבוי של tomato detector + frame-level tracking
Y              - הפעלה / כיבוי של E-STOP
START          - חזרה לתפריט הראשי
BACK           - יציאה מהתוכנית

כפתורי Joystick שפונו:
-----------------------
X      - פנוי עכשיו
L1     - פנוי עכשיו
R1     - פנוי עכשיו
L2     - פנוי עכשיו
R2     - פנוי עכשיו
D-PAD  - פנוי עכשיו

מיפוי מקלדת חדש בזמן שנמצאים במצב j וה-GUI פתוח:
--------------------------------------------------
q - toggle Mini LiDAR
w - toggle M5 IMU
s - toggle M5 ENV
a - reset local pose + yaw reference + odom
e - toggle local auto nav
u - הגדלת מהירות נסיעה CH1 / FB
j - הקטנת מהירות נסיעה CH1 / FB
i - הגדלת מהירות היגוי CH2 / LR
k - הקטנת מהירות היגוי CH2 / LR

מיפוי מקלדת ישן שנשאר פעיל במצב j:
------------------------------------
t - הגדלת GX ב-0.01m
g - הקטנת GX ב-0.01m
y - הגדלת GY ב-0.01m
h - הקטנת GY ב-0.01m
r - איפוס goal לברירת מחדל
z - toggle devFarm camera video recording up to 1GB
x - toggle devFarm LiDAR map recording to JSON
c - load latest devFarm LiDAR map from JSON
ESC / סגירת GUI - חזרה לתפריט

הערה חשובה:
E-STOP נשאר על Y ב-Joystick בכוונה, כדי לשמור כפתור בטיחות פיזי ומהיר.
