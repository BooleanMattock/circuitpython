/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2021 Jeff Epler for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "py/obj.h"
#include "py/runtime.h"

#include "lib/utils/context_manager_helpers.h"
#include "lib/utils/interrupt_char.h"

#include "shared-bindings/imagecapture/ParallelImageCapture.h"
#include "shared-bindings/microcontroller/__init__.h"
#include "shared-bindings/microcontroller/Pin.h"
#include "common-hal/imagecapture/ParallelImageCapture.h"

#include "hal/include/hal_gpio.h"
#include "atmel_start_pins.h"

#define GPIO_PIN_FUNCTION_PCC (GPIO_PIN_FUNCTION_K)

#define PIN_PCC_D0 (PIN_PA16)
#define PIN_PCC_DEN1 (PIN_PA12)
#define PIN_PCC_DEN2 (PIN_PA13)
#define PIN_PCC_CLK (PIN_PA14)

void common_hal_imagecapture_parallelimagecapture_construct(imagecapture_parallelimagecapture_obj_t *self,
    const mcu_pin_obj_t *data0,
    const mcu_pin_obj_t *data_clock,
    const mcu_pin_obj_t *vertical_sync,
    const mcu_pin_obj_t *horizontal_reference,
    int data_count)
{
    if (data0->number != PIN_PCC_D0) {
        mp_raise_ValueError_varg(translate("Invalid %q pin"), MP_QSTR_data0);
    }
    // The peripheral supports 8, 10, 12, or 14 data bits, but the code only supports 8 at present
    if (data_count != 8)
    {
        mp_raise_ValueError_varg(translate("Invalid data_count %d"), data_count);
    }
    if (vertical_sync && vertical_sync->number != PIN_PCC_DEN1) {
        mp_raise_ValueError_varg(translate("Invalid %q pin"), MP_QSTR_vsync);
    }
    if (horizontal_reference && horizontal_reference->number != PIN_PCC_DEN2) {
        mp_raise_ValueError_varg(translate("Invalid %q pin"), MP_QSTR_href);
    }
    if (data_clock->number != PIN_PCC_CLK) {
        mp_raise_ValueError_varg(translate("Invalid %q pin"), MP_QSTR_data_clock);
    }
    // technically, 0 was validated as free already but check again
    for (int i=0; i<data_count; i++) {
        if (!pin_number_is_free(data0->number + i)) {
            mp_raise_ValueError_varg(translate("data pin #%d in use"), i);
        }
    }

    PCC->MR.bit.PCEN = 0; // Make sure PCC is disabled before setting MR reg

    PCC->IDR.reg = 0b1111;       // Disable all PCC interrupts
    MCLK->APBDMASK.bit.PCC_ = 1; // Enable PCC clock

    // Accumulate 4 bytes into RHR register (two 16-bit pixels)
    PCC->MR.reg = PCC_MR_CID(0x1) | // Clear on falling DEN1 (VSYNC)
                PCC_MR_ISIZE(0x0) | // Input data bus is 8 bits
                PCC_MR_DSIZE(0x2);  // "4 data" at a time (accumulate in RHR)

    PCC->MR.bit.PCEN = 1; // Enable PCC


    // Now we know we can allocate all pins
    self->data_count = data_count;
    self->vertical_sync = vertical_sync ? vertical_sync->number : NO_PIN;
    self->horizontal_reference = horizontal_reference ? vertical_sync->number : NO_PIN;
    gpio_set_pin_direction(PIN_PCC_CLK, GPIO_DIRECTION_IN);
    gpio_set_pin_pull_mode(PIN_PCC_CLK, GPIO_PULL_OFF);
    gpio_set_pin_function(PIN_PCC_CLK, GPIO_PIN_FUNCTION_PCC);
    //claim_pin_number(PIN_PCC_CLK);
    if (vertical_sync) {
        gpio_set_pin_direction(PIN_PCC_DEN1, GPIO_DIRECTION_IN);
        gpio_set_pin_pull_mode(PIN_PCC_DEN1, GPIO_PULL_OFF);
        gpio_set_pin_function(PIN_PCC_DEN1, GPIO_PIN_FUNCTION_PCC); // VSYNC
        //claim_pin_number(PIN_PCC_DEN1);
    }
    if (horizontal_reference) {
        gpio_set_pin_direction(PIN_PCC_DEN2, GPIO_DIRECTION_IN);
        gpio_set_pin_pull_mode(PIN_PCC_DEN2, GPIO_PULL_OFF);
        gpio_set_pin_function(PIN_PCC_DEN2, GPIO_PIN_FUNCTION_PCC); // HSYNC
        //claim_pin_number(PIN_PCC_DEN2);
    }
    for (int i=0; i<data_count; i++) {
        gpio_set_pin_direction(PIN_PCC_D0+i, GPIO_DIRECTION_IN);
        gpio_set_pin_pull_mode(PIN_PCC_D0+i, GPIO_PULL_OFF);
        gpio_set_pin_function(PIN_PCC_D0+i, GPIO_PIN_FUNCTION_PCC);
        //claim_pin_number(PIN_PCC_D0+i);
    }
}

void common_hal_imagecapture_parallelimagecapture_deinit(imagecapture_parallelimagecapture_obj_t *self)
{
    if (common_hal_imagecapture_parallelimagecapture_deinited(self)) {
        return;
    }
    reset_pin_number(self->vertical_sync);
    reset_pin_number(self->horizontal_reference);
    reset_pin_number(PIN_PCC_CLK);
    for (int i=0; i<self->data_count; i++) {
        reset_pin_number(PIN_PCC_D0 + i);
    }
    self->data_count = 0;
}

bool common_hal_imagecapture_parallelimagecapture_deinited(imagecapture_parallelimagecapture_obj_t *self)
{
    return self->data_count == 0;
}

void common_hal_imagecapture_parallelimagecapture_capture(imagecapture_parallelimagecapture_obj_t *self, void *buffer, size_t bufsize)
{
    uint32_t *dest = buffer;
    size_t count = bufsize / 4; // PCC receives 4 bytes (2 pixels) at a time

    const volatile uint32_t *vsync_reg = self->vertical_sync == NO_PIN ? NULL : &PORT->Group[(self->vertical_sync / 32)].IN.reg;
    uint32_t vsync_bit = 1 << (self->vertical_sync % 32);

    if (vsync_reg) {
        while (*vsync_reg & vsync_bit)
        {
            // Wait for VSYNC low (frame end)

            RUN_BACKGROUND_TASKS;
            // Allow user to break out of a timeout with a KeyboardInterrupt.
            if (mp_hal_is_interrupted()) {
                return;
            }
        }
    }

    common_hal_mcu_disable_interrupts();

    if (vsync_reg) {
        size_t j = 1000000; // Don't freeze forever (this is ballpark 100ms timeout)
        while (!(*vsync_reg & vsync_bit)) {
            // Wait for VSYNC high (frame start)
            if (!--j) {
                common_hal_mcu_enable_interrupts();
                mp_raise_RuntimeError(translate("Timeout waiting for VSYNC"));
            }
        }
    }

    // TODO: use DMA
    for (size_t i = 0; i<count; i++) {
        size_t j = 1000000; // Don't freeze forever (this is ballpark 100ms timeout)
        while (!PCC->ISR.bit.DRDY) {
            if (!--j) {
                common_hal_mcu_enable_interrupts();
                mp_raise_RuntimeError(translate("Timeout waiting for DRDY"));
            }
            //    Wait for PCC data ready
        }
        *dest++ = PCC->RHR.reg; //    Store 2 pixels
    }

    common_hal_mcu_enable_interrupts();
}
