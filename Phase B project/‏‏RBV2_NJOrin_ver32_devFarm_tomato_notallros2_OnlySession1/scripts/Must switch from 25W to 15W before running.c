
הגדרת Jetson Orin Nano למצב 15W לפני הרצת הרובוט

בכל פעם שמדליקים את הרובוט / Jetson, לפני הרצת תוכנית הרובוט יש להריץ:

sudo nvpmodel -m 0
sudo nvpmodel -q

הפלט התקין צריך להיות:

NV Power Mode: 15W
0

אם מופיע 15W, אפשר להריץ את תוכנית הרובוט.

חשוב:
לא להריץ בזמן ניסויי הרובוט את הפקודה:

sudo jetson_clocks

הסיבה:
במערכת שלי מצב 25W או שימוש ב־jetson_clocks גרם להתראת:
System throttled due to Over-current

לכן בסביבת הרובוט והחממה עובדים כרגע במצב 15W בלבד.

בדיקה מהירה לפני ניסוי בחממה:

sudo nvpmodel -q

אם הפלט אינו 15W, להחזיר ל־15W עם:

sudo nvpmodel -m 0
sudo nvpmodel -q
```

פקודה שמריצה דאגנוסטיקה על משאבים וטמפרטורה 
tegrastats --interval 1000
