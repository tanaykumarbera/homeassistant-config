/** /////// rgbstriplight.h
Copyright 2020 - tanaykumarbera@gmail.com
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "esphome.h"

#define BRIGHTNESS_OFF                  0.0
#define BRIGHTNESS_MAX                  1.0

#define NEC_ADDRESS                     0xF7

#define NEC_CODE_BTN_OFF                0x40BF 
#define NEC_CODE_BTN_ON                 0xC03F
#define NEC_CODE_BTN_WHITE              0xE01F
#define NEC_CODE_BTN_BRIGHTNESS_PLUS    0x00FF
#define NEC_CODE_BTN_BRIGHTNESS_MINUS   0x807F

#define NEC_CODE_BTN_EFFECT_FLASH       0xD02F
#define NEC_CODE_BTN_EFFECT_STROBE      0xF00F
#define NEC_CODE_BTN_EFFECT_FADE        0xC837
#define NEC_CODE_BTN_EFFECT_SMOOTH      0xE817

#define NEC_CODE_BTN_R                  0x20DF 
#define NEC_CODE_BTN_G                  0xA05F 
#define NEC_CODE_BTN_B                  0x609F
#define NEC_CODE_BTN_1                  0x10EF 
#define NEC_CODE_BTN_2                  0x906F 
#define NEC_CODE_BTN_3                  0x50AF
#define NEC_CODE_BTN_4                  0x30CF 
#define NEC_CODE_BTN_5                  0xB04F 
#define NEC_CODE_BTN_6                  0x708F
#define NEC_CODE_BTN_7                  0x08F7 
#define NEC_CODE_BTN_8                  0x8877 
#define NEC_CODE_BTN_9                  0x48B7
#define NEC_CODE_BTN_10                 0x28D7 
#define NEC_CODE_BTN_11                 0xA857
#define NEC_CODE_BTN_12                 0x6897

/**
 * 
 * A custom component to control basic rgb strip like this
 * [insert link here]
 * 
 * Create a template light and pass the transmitter component
 * 
 * ```
 * 
 * remote_transmitter:
 *    id: esp_transmitter
 *    pin:
 *        number: D8
 *    carrier_duty_percent: 50%
 *
 * light:
 *    - platform: custom
 *      lambda: |-
 *          auto led_strip = new RGBStripLight(id(esp_transmitter));
 *          App.register_component(led_strip);
 *          return {led_strip};
 *      lights:
 *          - name: "LED Strip"
 *            default_transition_length: 0s
 * ``` 
 * */
class RGBStripLight : public Component, public LightOutput {

    private:
    remote_transmitter::RemoteTransmitterComponent *transmitter;
    remote_base::NECAction<> *nec_action;

    float last_relative_brightness  = 0.0;
    float last_relative_red         = 0.0;
    float last_relative_green       = 0.0;
    float last_relative_blue        = 0.0;

    /**
     *  Different shades which are supported by the strip
     *  
     *  This is not exact color, but would be more like a reference
     *  for SHADE_NEC_MAP to act upon. 
     * */
    int RGB_SHADES[15][3] = {
        { 246, 99,  87  },  { 11, 226, 135 },   { 68,  33, 251 },
        { 237, 190, 149 },  { 13, 235, 223 },   { 137, 88, 234 },
        { 240, 236, 176 },  { 18, 255, 254 },   { 206, 71, 252 },
        { 229, 248, 184 },  { 16, 230, 255 },   { 213, 50, 253 },
        { 200, 252, 179 },  { 13, 212, 254 },   { 229, 40, 253 }
    };

    /**
     * NEC code map for above shades. This might differ, but usually
     * remain same for most of the NEC IR transmitters out there.
     * 
     * */
    int SHADE_NEC_MAP[15] = {
        NEC_CODE_BTN_R,     NEC_CODE_BTN_G,     NEC_CODE_BTN_B,
        NEC_CODE_BTN_1,     NEC_CODE_BTN_2,     NEC_CODE_BTN_3,
        NEC_CODE_BTN_4,     NEC_CODE_BTN_5,     NEC_CODE_BTN_6,
        NEC_CODE_BTN_7,     NEC_CODE_BTN_8,     NEC_CODE_BTN_9,
        NEC_CODE_BTN_10,    NEC_CODE_BTN_11,    NEC_CODE_BTN_12
    };

    /**
     * Resolve a provided rgb value to closest shade in
     * RGBShades.
     * 
     * Since we have to perform cont size lookup,
     * using euclidean's algo to find nearest match.
     * https://en.wikipedia.org/wiki/Euclidean_distance
     * */
    int resolve_shade(float r, float g, float b) {
        int minDiff = INT_MAX;
        int minPosition = 0;
        int numShades = 15;
        for (int i = 0; i < numShades; i++) {
            int *shade = RGB_SHADES[i];
            float dR = r - shade[0];
            float dG = g - shade[1];
            float dB = b - shade[2];
            int diff = (int) sqrt(dR*dR + dG*dG + dB*dB);
            if (diff < minDiff) {
                minDiff = diff;
                minPosition = i;
            }
        }
        return minPosition;
    }

    /**
     * Maps a color to 255 based value
     * v is in [0.xxx ~ 1.000]
     * accept max out of R/G/B to get an idea of current brightness.
     * Convert v relative to max
     * */
    float resolve_intensity(float v, float max) {
        return (v / max) * 255;
    }

    /**
     * Update and send this command
     * via the NECAction created earlier
     * */
    void send_cmd(int cmd) {
        if (nec_action == NULL) return;
        nec_action->set_command(cmd);
        nec_action->play();
    }

    /**
     * Update and send this command
     * via the NECAction created earlier
     * */
    void send_cmd(int cmd, int times) {
        if (nec_action == NULL) return;
        while (times-- >= 0) {
            // Todo: Check if can be achieved in non blocking way
            nec_action->set_command(cmd);
            nec_action->play();
            delay(100);
        }
    }

    bool is_brightness_adjusted(float r, float g, float b, float brightness) {
        float ALLOWED_DELTA = 0.01;
        return (fabs(r - last_relative_red) < ALLOWED_DELTA)
            && (fabs(g - last_relative_green) < ALLOWED_DELTA)
            && (fabs(b - last_relative_blue) < ALLOWED_DELTA)
            && (fabs(brightness - last_relative_brightness) > ALLOWED_DELTA);
    }

    public:
    RGBStripLight(remote_transmitter::RemoteTransmitterComponent *component) {
        transmitter = component;
    }

    void setup() override {
        // configure this component
        nec_action = new remote_base::NECAction<>();
        nec_action->set_parent(transmitter);
        nec_action->set_address(NEC_ADDRESS);
    }

    /**
     * Create and return a light trait
     * for the controller
     * */
    LightTraits get_traits() override {
        auto traits = LightTraits();
        traits.set_supports_brightness(true);
        traits.set_supports_rgb(true);
        traits.set_supports_rgb_white_value(true);
        traits.set_supports_color_temperature(false);
        return traits;
    }

    void write_state(LightState *state) override {

        // if last brightness was 0, consider turning on as well
        if (last_relative_brightness == 0.0) send_cmd(NEC_CODE_BTN_ON);

        // read R/G/B
        float red, green, blue;
        state->current_values_as_rgb(&red, &green, &blue);

        // fetch max out of R - G - B
        float max = ((red > green ? red : green) > blue ? (red > green ? red : green) : blue);

        int newRed = resolve_intensity(red, max);
        int newGreen = resolve_intensity(green, max);
        int newBlue = resolve_intensity(blue, max);

        if (max == BRIGHTNESS_OFF) {
            // switch it off
            send_cmd(NEC_CODE_BTN_OFF);
        } else if (is_brightness_adjusted(newRed, newGreen, newBlue, max)) {
            // brightness changed. calc offset and send cmd
            // 40 is kind of a magic number. Since this brightness cannot be mapped 
            // It's relative. Please adjust this if required.
            int step = (int) (fabs(max - last_relative_brightness) * 40);
            if (last_relative_brightness > max) {
                send_cmd(NEC_CODE_BTN_BRIGHTNESS_MINUS, step);
            } else {
                send_cmd(NEC_CODE_BTN_BRIGHTNESS_PLUS, step);
            }
        } else if (red == green && red == blue) {
            // white light
            send_cmd(NEC_CODE_BTN_WHITE);
        } else {
            // resolve nearest supported shade
            int nearest_shade_position = resolve_shade(newRed, newGreen, newBlue);
            send_cmd(SHADE_NEC_MAP[nearest_shade_position]);
        }

        // store max to check on last brightness
        last_relative_red = newRed;
        last_relative_green = newGreen;
        last_relative_blue = newBlue;
        last_relative_brightness = max;
    }
};