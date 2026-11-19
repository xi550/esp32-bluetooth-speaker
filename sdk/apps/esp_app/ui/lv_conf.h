#ifndef LV_CONF_H
#define LV_CONF_H

/* 单任务、RGB565 软件绘制；内存按需分配，不预留整屏缓存。 */
#define LV_COLOR_DEPTH           16
#define LV_USE_OS                0
#define LV_USE_STDLIB_MALLOC     1
#define LV_USE_STDLIB_STRING     1
#define LV_USE_STDLIB_SPRINTF    1
#define LV_DEF_REFR_PERIOD       33
#define LV_DRAW_SW_DRAW_UNIT_CNT 1
#define LV_USE_LOG               0
#define LV_USE_THEME_DEFAULT     0
#define LV_USE_THEME_SIMPLE      0
#define LV_USE_THEME_MONO        0
#define LV_USE_FLEX              0
#define LV_USE_GRID              0
#define LV_BUILD_EXAMPLES        0
#define LV_BUILD_DEMOS           0
#define LV_FONT_MONTSERRAT_28    1
#define LV_FONT_MONTSERRAT_20    1
#define LV_FONT_FMT_TXT_LARGE    1
#define LV_USE_FONT_COMPRESSED   1

/* 仅使用标签、进度条与基础对象，关闭未用控件及其布局依赖。 */
#define LV_USE_ANIMIMG      0
#define LV_USE_ARC          0
#define LV_USE_BAR          1
#define LV_USE_BUTTON       0
#define LV_USE_BUTTONMATRIX 0
#define LV_USE_CALENDAR     0
#define LV_USE_CANVAS       0
#define LV_USE_CHART        0
#define LV_USE_CHECKBOX     0
#define LV_USE_DROPDOWN     0
#define LV_USE_IMAGE        0
#define LV_USE_IMAGEBUTTON  0
#define LV_USE_KEYBOARD     0
#define LV_USE_LABEL        1
#define LV_USE_LED          0
#define LV_USE_LINE         0
#define LV_USE_LIST         0
#define LV_USE_LOTTIE       0
#define LV_USE_MENU         0
#define LV_USE_MSGBOX       0
#define LV_USE_ROLLER       0
#define LV_USE_SCALE        0
#define LV_USE_SLIDER       0
#define LV_USE_SPAN         0
#define LV_USE_SPINBOX      0
#define LV_USE_SPINNER      0
#define LV_USE_SWITCH       0
#define LV_USE_TABLE        0
#define LV_USE_TABVIEW      0
#define LV_USE_TEXTAREA     0
#define LV_USE_TILEVIEW     0
#define LV_USE_WIN          0
#define LV_USE_3DTEXTURE    0

#endif
