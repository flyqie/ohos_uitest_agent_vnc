/*
 * Copyright (c) 2024 Huawei Device Co., Ltd.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef EXTENSION_C_API_H
#define EXTENSION_C_API_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>  // For va_list

// Define return type and error codes
typedef int32_t RetCode;
#define RETCODE_SUCCESS 0
#define RETCODE_FAIL (-1)

// Define a Text structure
struct Text {
    const char *data;
    size_t size;
};

// Define a ReceiveBuffer structure
struct ReceiveBuffer {
    uint8_t *data;
    size_t capacity;
    size_t *size;
};

// Define a DataCallback function pointer
typedef void (*DataCallback)(struct Text bytes);

// Define LowLevelFunctions structure
struct LowLevelFunctions {
    RetCode (*callThroughMessage)(struct Text in, struct ReceiveBuffer out, int32_t *fatalError);
    RetCode (*setCallbackMessageHandler)(DataCallback handler);
    RetCode (*atomicTouch)(int32_t stage, int32_t px, int32_t py);
    RetCode (*startCapture)(struct Text name, DataCallback callback, struct Text optJson);
    RetCode (*stopCapture)(struct Text name);
};

// Define UiTestPort structure
struct UiTestPort {
    RetCode (*getUiTestVersion)(struct ReceiveBuffer out);
    RetCode (*printLog)(int32_t level, struct Text tag, struct Text format, va_list ap);
    RetCode (*getAndClearLastError)(int32_t *codeOut, struct ReceiveBuffer msgOut);
    RetCode (*initLowLevelFunctions)(struct LowLevelFunctions *out);
};

// Define the function names for the UiTestExtension library
#define UITEST_EXTENSION_CALLBACK_ONINIT "UiTestExtension_OnInit"
#define UITEST_EXTENSION_CALLBACK_ONRUN "UiTestExtension_OnRun"

// Define the callback function types
typedef RetCode (*UiTestExtensionOnInitCallback)(struct UiTestPort port, size_t argc, char **argv);
typedef RetCode (*UiTestExtensionOnRunCallback)(void);

#endif // EXTENSION_C_API_H