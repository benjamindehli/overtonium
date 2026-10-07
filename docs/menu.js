/* Folds the section list into a menu on a narrow screen.

   Loaded in the head without defer, because its first line has to run before
   the page is drawn: it marks the document as having this script, and the
   stylesheet only collapses the list into a bar when that mark is there. Set
   any later, the list would draw open and then jump shut. With this file
   blocked the mark is never set and a phone gets the list as it always was,
   every link showing, so like the other scripts here it leaves nothing broken
   without it.

   Everything else waits for the header to exist. The button opens and closes
   the list, and so do the things a menu is expected to answer to: a link
   chosen inside it, Escape, and a tap anywhere outside it. The stylesheet
   decides the width it applies at, so a wide window simply never shows the
   button. */
(function () {
    "use strict";

    document.documentElement.setAttribute("data-menu", "");

    function wire() {
        var header = document.querySelector("header");
        var button = header && header.querySelector(".menu");
        var list = button && document.getElementById(button.getAttribute("aria-controls"));

        if (!list) return;

        function isOpen() {
            return button.getAttribute("aria-expanded") === "true";
        }

        function set(open) {
            button.setAttribute("aria-expanded", open ? "true" : "false");

            if (open) header.setAttribute("data-open", "");
            else header.removeAttribute("data-open");
        }

        button.addEventListener("click", function () {
            set(!isOpen());
        });

        /* A link to a part of this same page scrolls rather than loads, so
           the menu would otherwise stay open over the place it just went. */
        list.addEventListener("click", function (event) {
            if (event.target.closest("a")) set(false);
        });

        document.addEventListener("keydown", function (event) {
            if (event.key === "Escape" && isOpen()) {
                set(false);
                button.focus();
            }
        });

        document.addEventListener("click", function (event) {
            if (isOpen() && !header.contains(event.target)) set(false);
        });
    }

    if (document.readyState === "loading") document.addEventListener("DOMContentLoaded", wire);
    else wire();
})();
