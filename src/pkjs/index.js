// index.js — pkjs. Clay registers 'showConfiguration' and 'webviewclosed' on
// its own and sends the saved settings to the watch, so there is no handler to
// write here; nothing is fetched or computed on the phone.
var Clay = require("@rebble/clay");
var clayConfig = require("./config");

// Constructed for its effect: nothing here has a use for the instance.
new Clay(clayConfig);
