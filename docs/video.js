/* The only script on this site, and it exists to keep a promise the markup
   makes on its own: nothing here reaches a third party until somebody asks it
   to.

   A YouTube iframe contacts Google the moment the page carrying it opens,
   whether or not anybody presses play, and sets cookies on the way. So the
   video ships as a poster and a link, which is what it degrades to when this
   file does not load, and the embed is built on the click instead. The chapter
   links work the same way and hand their offset to the player rather than to
   youtube.com.

   Everything is found by data attribute rather than by id, so a page can carry
   one of these or several without the script knowing which. */
(function () {
    "use strict";

    var EMBED = "https://www.youtube-nocookie.com/embed/";

    /* The poster is inside the figure it belongs to. A chapter link is not, so
       it takes the first video in its own section, which is the one the list
       was written under. */
    function figureFor(link) {
        var own = link.closest(".video");
        if (own) {
            return own;
        }

        var section = link.closest("section");
        return section ? section.querySelector(".video") : null;
    }

    /* A middle click, or a click with a modifier held, means somebody asked for
       a new tab or a new window. That is the plain link doing its job and not
       something to intercept. */
    function wantsNewTab(event) {
        return event.button !== 0 || event.metaKey || event.ctrlKey || event.shiftKey || event.altKey;
    }

    function play(figure, id, start) {
        var source = EMBED + encodeURIComponent(id) + "?autoplay=1&rel=0" + (start ? "&start=" + encodeURIComponent(start) : "");
        var frame = figure.querySelector("iframe");

        if (frame) {
            frame.src = source;
        } else {
            frame = document.createElement("iframe");
            frame.src = source;
            frame.title = figure.dataset.title || "Video";
            frame.allow = "accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share";
            frame.allowFullscreen = true;
            frame.referrerPolicy = "strict-origin-when-cross-origin";
            figure.querySelector(".poster").replaceWith(frame);
        }

        /* A chapter link can sit below the fold from the player it drives, and
           clicking the replaced poster leaves the keyboard on nothing at all.
           Both are fixed by putting the player in view and giving it focus. */
        frame.scrollIntoView({ block: "nearest" });
        frame.focus();
    }

    document.querySelectorAll("[data-video]").forEach(function (link) {
        link.addEventListener("click", function (event) {
            if (wantsNewTab(event)) {
                return;
            }

            var figure = figureFor(link);
            if (!figure) {
                return;
            }

            event.preventDefault();
            play(figure, link.dataset.video, link.dataset.start);
        });
    });
})();
