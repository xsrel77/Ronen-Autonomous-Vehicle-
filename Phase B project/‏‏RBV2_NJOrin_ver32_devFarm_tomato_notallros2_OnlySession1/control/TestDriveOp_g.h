#pragma once


class DriveController;


// מפעיל "נהיגה אמיתית" עם evdev (Key Down/Up)
// דורש להריץ עם sudo (גישה ל-/dev/input/*).
void RunTestDriveOp_g(DriveController& drive);



