# ESP32_Frequency_Counter
ESP32 PCNT based frequency counter for HF (1-30 MHz) frequencies accurate within ~1k

Tested using an ESP32S2 with FT-710 radio between 1.8MHz-28MHz

The code offers a Mute facility for switching the frequency source on/off. It will set the given GPIO high/low for if your hardware can switch the frequency source on/off. It will only turn on and probe for short moments to reduce noice or interference. If you do not have control over the frequency source you can comment out the line "#define ZC_MUTE" at the top. It will then count all the time without pausing. In most situations this is fine (it's just an extra for when needed).

## How it works

The Pulse counter peripheral is used to count pulses. Your ESP32 variant needs to have this peripheral.

Two trigger points are initialised at PCNT_LOW_LIMIT and at PCNT_HIGH_LIMIT. At PCNT_LOW_LIMIT a timer is started and at PCNT_HIGH_LIMIT the timer value is read. This is done 5 times and the median value of these 5 counts is used to calculate the frequency.

Depending on your hardware (clock accuracy) you can set a small correction value which will be used to correct the duration when not completely accurate.
