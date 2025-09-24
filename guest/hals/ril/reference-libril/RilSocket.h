/*
* Copyright (C) 2014 The Android Open Source Project
*
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

#ifndef RIL_SOCKET_H_INCLUDED
#define RIL_SOCKET_H_INCLUDED
#include <libril/ril_ex.h>
#include "rilSocketQueue.h"
#include <ril_event.h>

/**
 * Abstract socket class representing sockets in rild.
 * <p>
 * This class performs the following functions :
 * <ul>
 *     <li> Start socket listen.
 *     <li> Handle socket listen and command callbacks.
 * </ul>
 */
class RilSocket {
    protected:

        /**
         * Socket name.
         */
        const char* name;

        /**
         * Socket id.
         */
        RIL_SOCKET_ID id;

    public:

        /**
         * Constructor.
         *
         * @param Socket name.
         * @param Socket id.
         */
        RilSocket(const char* socketName, RIL_SOCKET_ID socketId) {
            name = socketName;
            id = socketId;
        }

        /**
         * Get socket id.
         *
         * @return RIL_SOCKET_ID socket id.
         */
        RIL_SOCKET_ID getSocketId(void) {
            return id;
        }

        virtual ~RilSocket(){}
};

#endif
