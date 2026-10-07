/* Sends a link to a section at its old address on to the page it lives on
   now. Written by Tools/build_site.py from the ids on each page, so edit
   MOVED in Tools/site_data.py rather than this.

   The third script on this site, and like the other two it leaves nothing
   broken without it: a moved section's old address still opens the page it
   used to be on, at the top. */
(function () {
    "use strict";

    var MOVED = {
        automation: "../playing/",
        "bus-drive": "../effects/",
        chainDesc: "../effects/",
        chainTitle: "../effects/",
        character: "../effects/",
        clip: "../effects/",
        converter: "../effects/",
        echo: "../effects/",
        "every-parameter": "../playing/",
        global: "../effects/",
        legato: "../playing/",
        macros: "../playing/",
        "midi-learn": "../playing/",
        mpe: "../playing/",
        "output-meter": "../effects/",
        reverb: "../effects/",
        settings: "../playing/",
        wobble: "../effects/"
    };

    var id = decodeURIComponent(window.location.hash.slice(1));

    if (Object.prototype.hasOwnProperty.call(MOVED, id) && !document.getElementById(id)) {
        window.location.replace(MOVED[id] + "#" + id);
    }
})();
