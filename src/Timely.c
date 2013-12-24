#include <pebble.h>
#include "battbar.h"
//#include <math.h>

/*
 * If you fork this code and release the resulting app, please be considerate and change all the values in appinfo.json 
 *
 * DESCRIPTION
 *  This watchface shows the current date and current time in the top 'half',
 *    and then a small calendar w/ 3 weeks: last, current, and next week, in the bottom 'half'
 *  The statusbar at the top shows the connection status, charging, and battery level - and it will vibrate on link lost.
 *  The settings for the face are configurable using the new PebbleKit JS configuration page
 * END DESCRIPTION Section
 *
 */

static Window *window;

static Layer *battery_layer;
static Layer *datetime_layer;
static TextLayer *date_layer;
static TextLayer *time_layer;
static Layer *calendar_layer;
static Layer *statusbar;
static Layer *slot_top;
static Layer *slot_bot;

static BitmapLayer *bmp_connection_layer;
static GBitmap *image_connection_icon;
static GBitmap *image_noconnection_icon;
static TextLayer *text_connection_layer; // TODO: temporary?
static BitmapLayer *bmp_charging_layer;
static GBitmap *image_charging_icon;
static GBitmap *image_hourvibe_icon;
//static TextLayer *text_battery_layer;

static InverterLayer *inverter_layer;
// static InverterLayer *battery_meter_layer;

// battery info, instantiate to 'worst scenario' to prevent false hopes
/*
static uint8_t battery_meter = 4; // length of fill inside battery meter
*/
static bool battery_charging = false;
static bool battery_plugged = false;

// define the persistent storage key(s)
#define PK_SETTINGS      0x0

// define the appkeys used for appMessages
#define AK_STYLE_INV     0x0
#define AK_STYLE_DAY_INV 0x1
#define AK_STYLE_GRID    0x2
#define AK_VIBE_HOUR     0x3
#define AK_INTL_DOWO     0x4
#define AK_INTL_FMT_DATE 0x5
#define AK_STYLE_AM_PM   0x6

// primary coordinates
#define DEVICE_WIDTH        144
#define DEVICE_HEIGHT       168
#define LAYOUT_STAT           0 // 20 tall
#define LAYOUT_SLOT_TOP      24 // 72 tall
#define LAYOUT_SLOT_BOT      96 // 72 tall, 4px gap above
#define LAYOUT_SLOT_HEIGHT   72
/*
#define STAT_BATT_LEFT      100 // LEFT + WIDTH + NIB_WIDTH <= 143
#define STAT_BATT_TOP         4
#define STAT_BATT_WIDTH      40 // should be divisible by 9, after subtracting 4 (2 pixels/side for the 'border')
#define STAT_BATT_HEIGHT     15
#define STAT_BATT_NIB_WIDTH   3 // >= 3
#define STAT_BATT_NIB_HEIGHT  5 // >= 3
*/
#define STAT_BT_ICON_LEFT   100 // 0
#define STAT_BT_ICON_TOP      4 //  62 - left of time
#define STAT_CHRG_ICON_LEFT  80 // 130 - right of time
#define STAT_CHRG_ICON_TOP    2 //  62 - right of time

// relative coordinates (relative to SLOTs)
#define REL_CLOCK_DATE_LEFT       0
#define REL_CLOCK_DATE_TOP       -6
#define REL_CLOCK_DATE_HEIGHT    30 // date/time overlap, due to the way text is 'positioned'
#define REL_CLOCK_TIME_LEFT       0
#define REL_CLOCK_TIME_TOP       12
#define REL_CLOCK_TIME_HEIGHT    60 // date/time overlap, due to the way text is 'positioned'

#define SLOT_ID_CLOCK_1  0
#define SLOT_ID_CALENDAR 1
#define SLOT_ID_WEATHER  2
#define SLOT_ID_CLOCK_2  3

// Create a struct to hold our persistent settings...
typedef struct persist {
  uint8_t inverted;               // Invert display
  uint8_t day_invert;             // Invert colors on today's date
  uint8_t grid;                   // Show the grid
  uint8_t vibe_hour;              // vibrate at the top of the hour?
  uint8_t dayOfWeekOffset;        // first day of our week
  uint8_t date_format;            // date format
  uint8_t show_am_pm;             // Show AM/PM next to time
  uint8_t slot_one;               // item in slot 1 [T]
  uint8_t slot_two;               // item in slot 2 [B]
  uint8_t slot_three;             // item in slot 3 [T, doubletap]
  uint8_t slot_four;              // item in slot 4 [B, doubletap]
  uint8_t slot_five;              // item in slot 5 [T, tripletap]
  uint8_t slot_six;               // item in slot 6 [B, tripletap]
} __attribute__((__packed__)) persist;

persist settings = {
  .inverted   = 0, // no, dark
  .day_invert = 1, // yes
  .grid       = 1, // yes
  .vibe_hour  = 0, // no
  .dayOfWeekOffset = 0, // 0 - 6, Sun - Sat
  .date_format = 0, // Month DD, YYYY
  .show_am_pm = 0, // no AM/PM by default
  .slot_one   = 0, // clock_1
  .slot_two   = 1, // calendar
  .slot_three = 2, // TODO: weather
  .slot_four  = 3, // TODO: clock_2 (2nd timezone)
  // options for other slots: extend weather to take 2, moon, tides, travel to/home, context, etc.
  .slot_five  = 1, // TODO: calendar (test)
  .slot_six   = 0, // TODO: weather  (test)
};

// TODO - make persistent/configurable for localization 
const char daysOfWeek[7][3] = {"Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};

// How many days are/were in the month
int daysInMonth(int mon, int year)
{
    mon++; // dec = 0|12, lazily optimized

    // April, June, September and November have 30 Days
    if (mon == 4 || mon == 6 || mon == 9 || mon == 11) {
        return 30;
    } else if (mon == 2) {
        // Deal with Feburary & Leap years
        if (year % 400 == 0) {
            return 29;
        } else if (year % 100 == 0) {
            return 28;
        } else if (year % 4 == 0) {
            return 29;
        } else {
            return 28;
        }
    } else {
        // Most months have 31 days
        return 31;
    }
}

struct tm *get_time()
{
    time_t tt = time(0);
    return localtime(&tt);
}

void setColors(GContext* ctx){
    window_set_background_color(window, GColorBlack);
    graphics_context_set_stroke_color(ctx, GColorWhite);
    graphics_context_set_fill_color(ctx, GColorBlack);
    graphics_context_set_text_color(ctx, GColorWhite);
}

void setInvColors(GContext* ctx){
    window_set_background_color(window, GColorWhite);
    graphics_context_set_stroke_color(ctx, GColorBlack);
    graphics_context_set_fill_color(ctx, GColorWhite);
    graphics_context_set_text_color(ctx, GColorBlack);
}

void calendar_layer_update_callback(Layer *me, GContext* ctx) {
    (void)me;
    struct tm *currentTime = get_time();

    int mon = currentTime->tm_mon;
    int year = currentTime->tm_year + 1900;
    int daysThisMonth = daysInMonth(mon, year);
    int specialDay = currentTime->tm_wday - settings.dayOfWeekOffset;
    if (specialDay < 0) { specialDay += 7; }
    /* We're going to build an array to hold the dates to be shown in the calendar.
     *
     * There are five 'parts' we'll calculate for this (though since we only display 3 weeks, we'll only ever see at most 4 of them)
     *
     *   daysVisPrevMonth = days from the previous month that are visible
     *   daysPriorToToday = days before today (including any days from previous month)
     *   ( today )
     *   daysAfterToday   = days after today (including any days from next month)
     *   daysVisNextMonth = days from the following month that are visible
     *
     *  daysPriorToToday + 1 + daysAfterToday = 21, since we display exactly 3 weeks.
     */
    int show_last = 1; // show last week?
    int show_next = 1; // show next week?
    int calendar[21];
    int cellNum = 0;   // address for current day table cell: 0-20
    int daysVisPrevMonth = 0;
    int daysVisNextMonth = 0;
    int daysPriorToToday = 7 + currentTime->tm_wday - settings.dayOfWeekOffset;
    int daysAfterToday   = 6 - currentTime->tm_wday + settings.dayOfWeekOffset;

    // tm_wday is based on Sunday being the startOfWeek, but Sunday may not be our startOfWeek.
    if (currentTime->tm_wday < settings.dayOfWeekOffset) { 
      if (show_last) {
        daysPriorToToday += 7; // we're <7, so in the 'first' week due to startOfWeek offset - 'add a week' before this one
      }
    } else {
      if (show_next) {
        daysAfterToday += 7;   // otherwise, we're already in the second week, so 'add a week' after
      }
    }

    if ( daysPriorToToday >= currentTime->tm_mday ) {
      // We're showing more days before today than exist this month
      int daysInPrevMonth = daysInMonth(mon - 1,year); // year only matters for February, which will be the same 'from' March

      // Number of days we'll show from the previous month
      daysVisPrevMonth = daysPriorToToday - currentTime->tm_mday + 1;

      for (int i = 0; i < daysVisPrevMonth; i++, cellNum++ ) {
        calendar[cellNum] = daysInPrevMonth + i - daysVisPrevMonth + 1;
      }
    }

    // optimization: instantiate i to a hot mess, since the first day we show this month may not be the 1st of the month
    int firstDayShownThisMonth = daysVisPrevMonth + currentTime->tm_mday - daysPriorToToday;
    for (int i = firstDayShownThisMonth; i < currentTime->tm_mday; i++, cellNum++ ) {
      calendar[cellNum] = i;
    }

    //int currentDay = cellNum; // the current day... we'll style this special
    calendar[cellNum] = currentTime->tm_mday;
    cellNum++;

    if ( currentTime->tm_mday + daysAfterToday > daysThisMonth ) {
      daysVisNextMonth = currentTime->tm_mday + daysAfterToday - daysThisMonth;
    }

    // add the days after today until the end of the month/next week, to our array...
    int daysLeftThisMonth = daysAfterToday - daysVisNextMonth;
    for (int i = 0; i < daysLeftThisMonth; i++, cellNum++ ) {
      calendar[cellNum] = i + currentTime->tm_mday + 1;
    }

    // add any days in the next month to our array...
    for (int i = 0; i < daysVisNextMonth; i++, cellNum++ ) {
      calendar[cellNum] = i + 1;
    }

// ---------------------------
// Now that we've calculated which days go where, we'll move on to the display logic.
// ---------------------------

    #define CAL_DAYS   7   // number of columns (days of the week)
    #define CAL_WIDTH  20  // width of columns
    #define CAL_GAP    1   // gap around calendar
    #define CAL_LEFT   2   // left side of calendar
    #define CAL_HEIGHT 18  // How tall rows should be depends on how many weeks there are

    int weeks  =  3;  // always display 3 weeks: previous, current, next
    if (!show_last) { weeks--; }
    if (!show_next) { weeks--; }
        
    GFont normal = fonts_get_system_font(FONT_KEY_GOTHIC_14); // fh = 16
    GFont bold   = fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD); // fh = 22
    GFont current = normal;
    int font_vert_offset = 0;

    // generate a light background for the calendar grid
    setInvColors(ctx);
    graphics_fill_rect(ctx, GRect (CAL_LEFT + CAL_GAP, CAL_HEIGHT - CAL_GAP, DEVICE_WIDTH - 2 * (CAL_LEFT + CAL_GAP), CAL_HEIGHT * weeks), 0, GCornerNone);
    setColors(ctx);
    for (int col = 0; col < CAL_DAYS; col++) {

      // Adjust labels by specified offset
      int weekday = col + settings.dayOfWeekOffset;
      if (weekday > 6) { weekday -= 7; }

      if (col == specialDay) {
        current = bold;
        font_vert_offset = -3;
      }
      // draw the cell background
      graphics_fill_rect(ctx, GRect (CAL_WIDTH * col + CAL_LEFT + CAL_GAP, 0, CAL_WIDTH - CAL_GAP, CAL_HEIGHT - CAL_GAP), 0, GCornerNone);

      // draw the cell text
      graphics_draw_text(ctx, daysOfWeek[weekday], current, GRect(CAL_WIDTH * col + CAL_LEFT + CAL_GAP, CAL_GAP + font_vert_offset, CAL_WIDTH, CAL_HEIGHT), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); 
      if (col == specialDay) {
        current = normal;
        font_vert_offset = 0;
      }
    }

    // draw the individual calendar rows/columns
    int week = 0;
    for (int row = 1; row <= 3; row++) {
      if (row == 1 && !show_last) { continue; }
      if (row == 3 && !show_next) { continue; }
      week++;
      for (int col = 0; col < CAL_DAYS; col++) {
        if ( row == 2 && col == specialDay) {
          setInvColors(ctx);
          current = bold;
          font_vert_offset = -3;
        }

        // draw the cell background
        graphics_fill_rect(ctx, GRect (CAL_WIDTH * col + CAL_LEFT + CAL_GAP, CAL_HEIGHT * week, CAL_WIDTH - CAL_GAP, CAL_HEIGHT - CAL_GAP), 0, GCornerNone);

        // draw the cell text
        char date_text[3];
        snprintf(date_text, sizeof(date_text), "%d", calendar[col + 7 * (row - 1)]);
        graphics_draw_text(ctx, date_text, current, GRect(CAL_WIDTH * col + CAL_LEFT, CAL_HEIGHT * week - CAL_GAP + font_vert_offset, CAL_WIDTH, CAL_HEIGHT), GTextOverflowModeWordWrap, GTextAlignmentCenter, NULL); 

        if ( row == 2 && col == specialDay) {
          setColors(ctx);
          current = normal;
          font_vert_offset = 0;
        }
      }
    }
}

void update_date_text()
{
    struct tm *currentTime = get_time();

    // TODO - 18 @ this font is approaching the max width, localization may require smaller fonts, or no year...
    //September 11, 2013 => 18 chars
    //123456789012345678
    static char date_text[20];
    // http://www.cplusplus.com/reference/ctime/strftime/
    strftime(date_text, sizeof(date_text), "%B %d, %Y", currentTime); // Month DD, YYYY
    //strftime(date_text, sizeof(date_text), "%d.%m.%Y", currentTime);  // DD.MM.YYYY

    text_layer_set_text(date_layer, date_text);
}

void update_time_text() {
  // Need to be static because used by the system later.
  static char time_text[] = "00:00";

  char *time_format;

  if (clock_is_24h_style()) {
    time_format = "%R";
  } else {
    time_format = "%I:%M";
  }

  struct tm *currentTime = get_time();

  strftime(time_text, sizeof(time_text), time_format, currentTime);

  // Kludge to handle lack of non-padded hour format string
  // for twelve hour clock.
  if (!clock_is_24h_style() && (time_text[0] == '0')) {
    memmove(time_text, &time_text[1], sizeof(time_text) - 1);
  }

  // I would love to just use clock_copy_time_string, but it refuses to center properly in 12-hour time (see Kludge above).
  //clock_copy_time_string(time_text, sizeof(time_text));
  text_layer_set_text(time_layer, time_text);

}

void datetime_layer_update_callback(Layer *me, GContext* ctx) {
    (void)me;

    setColors(ctx);
    update_date_text();
    update_time_text();
}

void statusbar_layer_update_callback(Layer *me, GContext* ctx) {
// XXX positioning tests... only valid if we leave statusbar's frame/bounds set to the whole watch...
/*
    setColors(ctx);
    graphics_draw_rect(ctx, GRect(0,  0, 144, 24)); // statusbar
    graphics_draw_rect(ctx, GRect(0, 24, 144, 72)); // top half
    graphics_draw_rect(ctx, GRect(0, 96, 144, 72)); // bottom half
*
    graphics_draw_rect(ctx, GRect(0, 50, 20, 20)); // linked
    graphics_draw_rect(ctx, GRect(0, 72, 20, 20)); // icon 2
    graphics_draw_rect(ctx, GRect(0, 50, 10, 42)); // battery l
    graphics_draw_rect(ctx, GRect(144-10, 50, 10, 42)); // battery r
    graphics_draw_rect(ctx, GRect(144-20, 50, 20, 20)); // icon 3
    graphics_draw_rect(ctx, GRect(144-20, 72, 20, 20)); // icon 4
    graphics_draw_rect(ctx, GRect(0, 46, 144, 50)); // targeting time
*/
}

void slot_top_layer_update_callback(Layer *me, GContext* ctx) {
// TODO: configurable: draw appropriate slot
}

void slot_bot_layer_update_callback(Layer *me, GContext* ctx) {
// TODO: configurable: draw appropriate slot
}

void battery_layer_update_callback(Layer *me, GContext* ctx) {
// simply draw the battery outline here - the text is a different layer, and we then 'fill' it with an inverterLayer
//  if (charge_state.is_charging) {
//    snprintf(battery_text, sizeof(battery_text), "%d%%++", charge_state.charge_percent);
//  } else {
//  if (charge_state.is_plugged) { ; } // plugged but not charging = warn user
//    snprintf(battery_text, sizeof(battery_text), "%d%%", charge_state.charge_percent);
//  }
  setColors(ctx);
// battery outline
//  graphics_draw_rect(ctx, GRect(STAT_BATT_LEFT, STAT_BATT_TOP, STAT_BATT_WIDTH, STAT_BATT_HEIGHT));
// battery 'nib' terminal
/*  graphics_draw_rect(ctx, GRect(STAT_BATT_LEFT + STAT_BATT_WIDTH - 1,
                                STAT_BATT_TOP + (STAT_BATT_HEIGHT - STAT_BATT_NIB_HEIGHT)/2,
                                STAT_BATT_NIB_WIDTH,
                                STAT_BATT_NIB_HEIGHT));
*/
// fill it in with current power
//  setInvColors(ctx);
//  graphics_fill_rect(ctx, GRect(72+22+2, 6, battery_meter-4, 11), 0, GCornerNone);
}

static void handle_battery(BatteryChargeState charge_state) {
    /* Change these options to your liking */
    BBOptions options; /* Step 5 */
    options.position = BATTBAR_POSITION_TOP;
    options.direction = BATTBAR_DIRECTION_DOWN;
    options.color = BATTBAR_COLOR_WHITE;
    options.isWatchApp = false;
  	Layer *window_layer = window_get_root_layer(window);
    DrawBattBar(options, window_layer); /* Step 6 */
	/*
	static char battery_text[] = "100 ";

  battery_meter = charge_state.charge_percent/10*(STAT_BATT_WIDTH-4)/9;
  battery_charging = charge_state.is_charging;
  battery_plugged = charge_state.is_plugged;

  layer_set_bounds(inverter_layer_get_layer(battery_meter_layer), GRect(STAT_BATT_LEFT+2, STAT_BATT_TOP+2, battery_meter, STAT_BATT_HEIGHT-4));
  layer_set_hidden(inverter_layer_get_layer(battery_meter_layer), false);

  if (charge_state.is_charging) { // charging
    snprintf(battery_text, sizeof(battery_text), "%d", charge_state.charge_percent);
    layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), false);
    bitmap_layer_set_bitmap(bmp_charging_layer, image_charging_icon);
//    if (charge_state.is_plugged) {
//      vibes_short_pulse(); // XXX: testing that is_plugged is implemented
//    }
  } else {
    if (charge_state.is_plugged) { // plugged but not charging = charging complete...
      layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), true);
      snprintf(battery_text, sizeof(battery_text), "%d", charge_state.charge_percent);
      //vibes_short_pulse(); 
    } else { // normal wear
      if (settings.vibe_hour) {
        layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), false);
        bitmap_layer_set_bitmap(bmp_charging_layer, image_hourvibe_icon);
      } else {
        layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), true);
      }
      snprintf(battery_text, sizeof(battery_text), "%d", charge_state.charge_percent);
    }
  }
  text_layer_set_text(text_battery_layer, battery_text);
  layer_mark_dirty(battery_layer);
  */
}

static void handle_bluetooth(bool connected) {
  text_layer_set_text(text_connection_layer, connected ? "Linked" : "NO LINK");
  if (connected) {
    bitmap_layer_set_bitmap(bmp_connection_layer, image_connection_icon);
  } else {
    vibes_double_pulse();  // because, this is bad...
    bitmap_layer_set_bitmap(bmp_connection_layer, image_noconnection_icon);
  }
}

static void window_load(Window *window) {

  Layer *window_layer = window_get_root_layer(window);
  GRect bounds = layer_get_bounds(window_layer);

  //statusbar = layer_create(GRect(0,LAYOUT_STAT,DEVICE_WIDTH,LAYOUT_SLOT_TOP));
  statusbar = layer_create(GRect(0,0,DEVICE_WIDTH,DEVICE_HEIGHT));
  layer_set_update_proc(statusbar, statusbar_layer_update_callback);
  layer_add_child(window_layer, statusbar);
  GRect stat_bounds = layer_get_bounds(statusbar);

  slot_top = layer_create(GRect(0,LAYOUT_SLOT_TOP,DEVICE_WIDTH,LAYOUT_SLOT_BOT));
  layer_set_update_proc(slot_top, slot_top_layer_update_callback);
  layer_add_child(window_layer, slot_top);
  GRect slot_top_bounds = layer_get_bounds(slot_top);

  slot_bot = layer_create(GRect(0,LAYOUT_SLOT_BOT,DEVICE_WIDTH,DEVICE_HEIGHT));
  layer_set_update_proc(slot_bot, slot_bot_layer_update_callback);
  layer_add_child(window_layer, slot_bot);
  GRect slot_bot_bounds = layer_get_bounds(slot_bot);

  bmp_connection_layer = bitmap_layer_create( GRect(STAT_BT_ICON_LEFT, STAT_BT_ICON_TOP, 20, 20) );
  layer_add_child(statusbar, bitmap_layer_get_layer(bmp_connection_layer));
  image_connection_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BT_LINKED_ICON);
  image_noconnection_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BT_NOLINK_ICON);

  bmp_charging_layer = bitmap_layer_create( GRect(STAT_CHRG_ICON_LEFT, STAT_CHRG_ICON_TOP, 20, 20) );
  layer_add_child(statusbar, bitmap_layer_get_layer(bmp_charging_layer));
  image_charging_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_CHARGING_ICON);
  image_hourvibe_icon = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_HOURVIBE_ICON);
  if (settings.vibe_hour) {
    bitmap_layer_set_bitmap(bmp_charging_layer, image_hourvibe_icon);
  } else {
    layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), true);
  }
/*
  battery_layer = layer_create(stat_bounds);
  layer_set_update_proc(battery_layer, battery_layer_update_callback);
  layer_add_child(statusbar, battery_layer);
*/
  datetime_layer = layer_create(slot_top_bounds);
  layer_set_update_proc(datetime_layer, datetime_layer_update_callback);
  layer_add_child(slot_top, datetime_layer);

  calendar_layer = layer_create(slot_bot_bounds);
  layer_set_update_proc(calendar_layer, calendar_layer_update_callback);
  layer_add_child(slot_bot, calendar_layer);

  date_layer = text_layer_create( GRect(REL_CLOCK_DATE_LEFT, REL_CLOCK_DATE_TOP, DEVICE_WIDTH, REL_CLOCK_DATE_HEIGHT) );
  text_layer_set_text_color(date_layer, GColorWhite);
  text_layer_set_background_color(date_layer, GColorClear);
  text_layer_set_font(date_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
  text_layer_set_text_alignment(date_layer, GTextAlignmentCenter);
  layer_add_child(datetime_layer, text_layer_get_layer(date_layer));

  time_layer = text_layer_create( GRect(REL_CLOCK_TIME_LEFT, REL_CLOCK_TIME_TOP, DEVICE_WIDTH, REL_CLOCK_TIME_HEIGHT) );
  text_layer_set_text_color(time_layer, GColorWhite);
  text_layer_set_background_color(time_layer, GColorClear);
  text_layer_set_font(time_layer, fonts_get_system_font(FONT_KEY_ROBOTO_BOLD_SUBSET_49));
  text_layer_set_text_alignment(time_layer, GTextAlignmentCenter);
  layer_add_child(datetime_layer, text_layer_get_layer(time_layer));

  // TODO: temporary?
  text_connection_layer = text_layer_create( GRect(20, 0, 72, 22) );
  text_layer_set_text_color(text_connection_layer, GColorWhite);
  text_layer_set_background_color(text_connection_layer, GColorClear);
  text_layer_set_font(text_connection_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
  text_layer_set_text_alignment(text_connection_layer, GTextAlignmentLeft);
  text_layer_set_text(text_connection_layer, "NO LINK");
  layer_add_child(statusbar, text_layer_get_layer(text_connection_layer));
/*
  text_battery_layer = text_layer_create( GRect(STAT_BATT_LEFT, STAT_BATT_TOP-2, STAT_BATT_WIDTH, STAT_BATT_HEIGHT) );
  text_layer_set_text_color(text_battery_layer, GColorWhite);
  text_layer_set_background_color(text_battery_layer, GColorClear);
  text_layer_set_font(text_battery_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(text_battery_layer, GTextAlignmentCenter);
  text_layer_set_text(text_battery_layer, "?");

  layer_add_child(statusbar, text_layer_get_layer(text_battery_layer));
*/

  // NOTE: No more adding layers below here - the inverter layers NEED to be the last to be on top!

  // hide battery meter, until we can fix the size/position later when subscribing
/*
  battery_meter_layer = inverter_layer_create(stat_bounds);
  layer_set_hidden(inverter_layer_get_layer(battery_meter_layer), true);
  layer_add_child(statusbar, inverter_layer_get_layer(battery_meter_layer));
*/
  // topmost inverter layer, determines dark or light...
  inverter_layer = inverter_layer_create(bounds);
  if (settings.inverted==0) {
    layer_set_hidden(inverter_layer_get_layer(inverter_layer), true);
  }
  layer_add_child(window_layer, inverter_layer_get_layer(inverter_layer));

}

static void window_unload(Window *window) {
  // unload anything we loaded, destroy anything we created, remove anything we added
  layer_destroy(inverter_layer_get_layer(inverter_layer));
//  layer_destroy(inverter_layer_get_layer(battery_meter_layer));
//  layer_destroy(text_layer_get_layer(text_battery_layer));
  layer_destroy(text_layer_get_layer(text_connection_layer)); // TODO: temporary?
  layer_destroy(text_layer_get_layer(time_layer));
  layer_destroy(text_layer_get_layer(date_layer));
  layer_destroy(calendar_layer);
  layer_destroy(datetime_layer);
//  layer_destroy(battery_layer);
  layer_remove_from_parent(bitmap_layer_get_layer(bmp_charging_layer));
  layer_remove_from_parent(bitmap_layer_get_layer(bmp_connection_layer));
  bitmap_layer_destroy(bmp_charging_layer);
  bitmap_layer_destroy(bmp_connection_layer);
  gbitmap_destroy(image_connection_icon);
  gbitmap_destroy(image_noconnection_icon);
  gbitmap_destroy(image_charging_icon);
  gbitmap_destroy(image_hourvibe_icon);
  layer_destroy(slot_bot);
  layer_destroy(slot_top);
  layer_destroy(statusbar);
}

static void deinit(void) {
  // deinit anything we init
  bluetooth_connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  tick_timer_service_unsubscribe();
  window_destroy(window);
}

void handle_minute_tick(struct tm *tick_time, TimeUnits units_changed)
{
  update_time_text();

  if (units_changed & MONTH_UNIT) {
    update_date_text();
  }

  if ((units_changed & HOUR_UNIT) && settings.vibe_hour) {
    vibes_short_pulse();
  }

  if (units_changed & DAY_UNIT) {
    layer_mark_dirty(datetime_layer);
    layer_mark_dirty(calendar_layer);
  }

  // TODO Confirm: calendar gets redrawn every time because time_layer is changed and all layers are redrawn together.
}

void my_out_sent_handler(DictionaryIterator *sent, void *context) {
// outgoing message was delivered
}
void my_out_fail_handler(DictionaryIterator *failed, AppMessageResult reason, void *context) {
// outgoing message failed
}

void my_in_rcv_handler(DictionaryIterator *received, void *context) {
// incoming message received
    // style_inv == inverted
    Tuple *style_inv = dict_find(received, AK_STYLE_INV); // TODO: bundle in single uint8_t?
    if (style_inv != NULL) {
      settings.inverted = style_inv->value->uint8;
//      if (strcmp(style_inv->value->cstring, "0")==0) { //}
      if (style_inv->value->uint8==0) {
        layer_set_hidden(inverter_layer_get_layer(inverter_layer), true); // hide inversion = dark
      } else {
        layer_set_hidden(inverter_layer_get_layer(inverter_layer), false); // show inversion = light
      }
    }

    // style_day_inv == day_invert // TODO
    Tuple *style_day_inv = dict_find(received, AK_STYLE_DAY_INV); // TODO: bundle in single uint8_t?
    if (style_day_inv != NULL) {
      settings.day_invert = style_day_inv->value->uint8;
    }

    // style_grid == grid // TODO: bundle in single uint8_t?
    Tuple *style_grid = dict_find(received, AK_STYLE_GRID);
    if (style_grid != NULL) {
      settings.grid = style_grid->value->uint8;
    }

    // int_vibe_hour == vibe_hour // TODO: bundle in single uint8_t?
    Tuple *vibe_hour = dict_find(received, AK_VIBE_HOUR);
    if (vibe_hour != NULL) {
      settings.vibe_hour = vibe_hour->value->uint8;
      if (settings.vibe_hour && !battery_plugged) {
        layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), false);
        bitmap_layer_set_bitmap(bmp_charging_layer, image_hourvibe_icon);
      } else if (!battery_charging) {
        layer_set_hidden(bitmap_layer_get_layer(bmp_charging_layer), true);
      }
    }

    // INTL_DOWO == dayOfWeekOffset
    Tuple *INTL_DOWO = dict_find(received, AK_INTL_DOWO);
    if (INTL_DOWO != NULL) {
      settings.dayOfWeekOffset = INTL_DOWO->value->uint8;
    }

    // INTL_DOW == daysOfWeek // TODO: localized Su Mo Tu We Th Fr Sa

    // INTL_MOY == monthsOfYear // TODO: localized month names, max ~9 characters ('September' == practical display limit)
    // INTL_format_date == // TODO

    // INTL_format_time == // TODO

    persist_write_data(PK_SETTINGS, &settings, sizeof(settings) );

    // ==== Implemented SDK ====
    // Battery
    // Connected
    // Persistent Storage
    // Screenshot Operation
    // ==== Available in SDK ====
    // Accelerometer
    // App Focus ( does this apply to Timely? )
    // ==== Waiting on / SDK gaps ====
    // Magnetometer
    // PebbleKit JS - more accurate location data
    // ==== Interesting SDK possibilities ====
    // PebbleKit JS - more information from phone
    // ==== Future improvements ====
    // Positioning - top, bottom, etc.
  if (1) { layer_mark_dirty(calendar_layer); }
  if (1) { layer_mark_dirty(datetime_layer); }

  //update_time_text(&currentTime);
}

void my_in_drp_handler(AppMessageResult reason, void *context) {
// incoming message dropped
}

static void app_message_init(void) {
  // Register message handlers
  app_message_register_inbox_received(my_in_rcv_handler);
  app_message_register_inbox_dropped(my_in_drp_handler);
  app_message_register_outbox_sent(my_out_sent_handler);
  app_message_register_outbox_failed(my_out_fail_handler);
  // Init buffers
  app_message_open(APP_MESSAGE_INBOX_SIZE_MINIMUM, APP_MESSAGE_OUTBOX_SIZE_MINIMUM);
}

static void init(void) {

  app_message_init();

  if (persist_exists(PK_SETTINGS)) {
    persist_read_data(PK_SETTINGS, &settings, sizeof(settings) );
  }

  window = window_create();
  window_set_window_handlers(window, (WindowHandlers) {
    .load = window_load,
    .unload = window_unload
  });
  const bool animated = false;
  window_set_background_color(window, GColorBlack);
  window_stack_push(window, animated);

  //update_time_text();

  tick_timer_service_subscribe(MINUTE_UNIT, &handle_minute_tick);
  battery_state_service_subscribe(&handle_battery);
  handle_battery(battery_state_service_peek()); // initialize
  bluetooth_connection_service_subscribe(&handle_bluetooth);
  handle_bluetooth(bluetooth_connection_service_peek()); // initialize
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
