/*
 * Copyright (c) 2017 Rodger Combs
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

var libmpv = require('bindings')('mpv.node');
var events = require('events');
var util = require('util');

function fillArray(type) {
  var obj = libmpv[type + 's'];
  var arr = [];
  for (var i in obj) {
    if (typeof obj[i] == 'number')
      arr[Math.abs(obj[i])] = i.toLowerCase();
  }
  libmpv[type + 'Array'] = arr;
}

libmpv.events = {
  NONE              : 0,
  SHUTDOWN          : 1,
  LOG_MESSAGE       : 2,
  GET_PROPERTY_REPLY : 3,
  SET_PROPERTY_REPLY : 4,
  COMMAND_REPLY     : 5,
  START_FILE        : 6,
  END_FILE          : 7,
  FILE_LOADED       : 8,
  IDLE              : 11,
  TICK              : 14,
  CLIENT_MESSAGE    : 16,
  VIDEO_RECONFIG    : 17,
  AUDIO_RECONFIG    : 18,
  SEEK              : 20,
  PLAYBACK_RESTART  : 21,
  PROPERTY_CHANGE   : 22,
  QUEUE_OVERFLOW    : 24
};

fillArray('event');

libmpv.errors = {
  SUCCESS           : 0,
  EVENT_QUEUE_FULL  : -1,
  NOMEM             : -2,
  UNINITIALIZED     : -3,
  INVALID_PARAMETER : -4,
  OPTION_NOT_FOUND  : -5,
  OPTION_FORMAT     : -6,
  OPTION_ERROR      : -7,
  PROPERTY_NOT_FOUND : -8,
  PROPERTY_FORMAT   : -9,
  PROPERTY_UNAVAILABLE : -10,
  PROPERTY_ERROR    : -11,
  COMMAND           : -12,
  LOADING_FAILED    : -13,
  AO_INIT_FAILED    : -14,
  VO_INIT_FAILED    : -15,
  NOTHING_TO_PLAY   : -16,
  UNKNOWN_FORMAT    : -17,
  UNSUPPORTED       : -18,
  NOT_IMPLEMENTED   : -19,
  GENERIC           : -20
};

fillArray('error');

libmpv.logLevels = {
  NONE  : 0,
  FATAL : 10,
  ERROR : 20,
  WARN  : 30,
  INFO  : 40,
  V     : 50,
  DEBUG : 60,
  TRACE : 70,
};

fillArray('logLevel');

util.inherits(libmpv.handle, events.EventEmitter);
libmpv.handle.prototype.setupEvents = function() {
  this.on('mpv_event', function (ev) {
    var name = libmpv.eventArray[ev.event_id];
    if (!name)
      name = 'event' + ev.event_id;
    this.emit(name, ev);
  });
}

module.exports = libmpv;
