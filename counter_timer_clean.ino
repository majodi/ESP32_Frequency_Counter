#include "driver/pulse_cnt.h"

#define ZC_MUTE 3                                           // zero crossing counter mute pin (mute frequency signal when not counting to avoid interference)

#define PULSE_GPIO 17

#define PCNT_LOW_LIMIT 2500
#define PCNT_HIGH_LIMIT 32500
#define DURATION_TIMER_MHZ 40

pcnt_unit_handle_t  pcnt_unit       = NULL;
hw_timer_t *        durationTimer   = NULL;
uint64_t            duration        = 0;
uint64_t            durations[]     = {0, 0, 0, 0, 0};
bool                countReady      = false;
int                 counted         = -1;                   // -1 means frequency is clocked in and valid
float               pcntCorrection  = -3.9;
float               frequency       = 0;
TaskHandle_t        pcntHandle;                             // perhaps for future use (handle of counter task)

static bool onCountReach(pcnt_unit_handle_t unit, const pcnt_watch_event_data_t *edata, void *user_ctx)
{
  if (edata->watch_point_value == PCNT_HIGH_LIMIT) {
    *(uint64_t *)user_ctx = timerRead(durationTimer);       // set duration value (pointer to duration is passed as context)
    countReady = true;                                      // inform pcnt task there was an update
  } else {
    timerRestart(durationTimer);                            // turn on stopwatch
  }
  return(false); // for wake-up (see docs)
}

void initPCNT() {
  int retval = 0;                                           // can be used to debug function calls
  pcnt_unit_config_t unit_config = {
    .low_limit   = -1,
    .high_limit  = PCNT_HIGH_LIMIT,
  };
  retval = pcnt_new_unit(&unit_config, &pcnt_unit);
  pcnt_chan_config_t chan_config = {
      .edge_gpio_num = PULSE_GPIO,
      .level_gpio_num = -1,                                 // level signal not used
  };
  pcnt_channel_handle_t pcnt_chan = NULL;
  retval = pcnt_new_channel(pcnt_unit, &chan_config, &pcnt_chan);
  retval = pcnt_unit_add_watch_point(pcnt_unit, PCNT_LOW_LIMIT);   // watchpoint for reaching low count
  retval = pcnt_unit_add_watch_point(pcnt_unit, PCNT_HIGH_LIMIT);  // watchpoint for reaching high count
  pcnt_event_callbacks_t cbs = {
      .on_reach = onCountReach,
  };
  retval = pcnt_unit_register_event_callbacks(pcnt_unit, &cbs, &duration);
  retval = pcnt_channel_set_edge_action(pcnt_chan, PCNT_CHANNEL_EDGE_ACTION_HOLD, PCNT_CHANNEL_EDGE_ACTION_INCREASE);
  retval = pcnt_unit_enable(pcnt_unit);
  retval = pcnt_unit_clear_count(pcnt_unit);
  durationTimer = timerBegin(DURATION_TIMER_MHZ * 1000000);
  timerWrite(durationTimer, 0);
  timerRestart(durationTimer);
}

void initIO() {
  delay(1500);                                              // time needed for ROM to setup CDC port
  Serial.begin(115200);
  Serial.setDebugOutput(true);  
#ifdef ZC_MUTE
  pinMode(ZC_MUTE, OUTPUT);
#endif
}

void updateFrequency() {
  counted++;                                                // first call to this update will increment the -1 (of last full count) to 0
  durations[counted] = duration;
  if (counted >= 4) {
    for (int i = 1; i < 5; ++i) {                           // sort last 5 counts to determine frequency of median time duration
      uint64_t key = durations[i];
      int j = i - 1;
      while (j >= 0 && durations[j] > key) {
          durations[j + 1] = durations[j];
          j = j - 1;
      }
      durations[j + 1] = key;
    }
    frequency = (float)(DURATION_TIMER_MHZ * 1000) / ((float)durations[2] / (PCNT_HIGH_LIMIT - PCNT_LOW_LIMIT + pcntCorrection)); // take median (middle) value to calculate frequency
    for (int i = 0; i < 5; ++i) {                           // clear last 5 counts
      durations[i] = 0;
    }
    counted = -1;                                           // signal that frequency clocked in after a full count
  }
}

void pcntOn() {
#ifdef ZC_MUTE
  digitalWrite(ZC_MUTE, LOW);                               // mute off
#endif
  vTaskDelay(25 / portTICK_PERIOD_MS);                      // give it some time to unmute
  pcnt_unit_clear_count(pcnt_unit);
  pcnt_unit_start(pcnt_unit);                               // (re)start counter
}

void resetWhenSilent(unsigned long lastTxMillis) {
    if ((millis() - lastTxMillis) > 50) {                   // 50ms no activity (ample time to have a full count at 1MHz) --> reset frequency and pause listening for a while (when Mute facility available)
      frequency = 0;                                        // reset frequency
#ifdef ZC_MUTE
      // turn off/on counter (if Mute facility available)
      digitalWrite(ZC_MUTE, HIGH);                          // mute to reduce noice where possible
      pcnt_unit_stop(pcnt_unit);                            // stop counter
    }
    if ((millis() - lastTxMillis) > 300) {                  // 300ms no activity (because we're probably not listening), start listening (when Mute option available)
      lastTxMillis = millis();                              // re-arm full count signaling
      pcntOn();                                             // turn on counter (if not already on)
#endif
    }
}

void pcntTask(void * parameter) {
  unsigned long lastTxMillis = millis();
  while(true) {
    if (countReady) {                                       // if one block count was done
      countReady = false;
      updateFrequency();                                    // add duration value to array and when 5 values, update frequency
      lastTxMillis = millis();                              // time having a full count (Tx on)
    }
    resetWhenSilent(lastTxMillis);                          // reset frequency when silent and handle mute/unmute when Mute facility available
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void setup() {
  initIO();                                                 // init IO
  initPCNT();                                               // init counter
  xTaskCreate(pcntTask, "pcntTask", 2048, NULL, 1, &pcntHandle);  // start counter task
  pcntOn();                                                 // turn on counter
}

void loop() {
  Serial.printf("Frequency: %f\n", round(frequency));
  delay(500);
}
