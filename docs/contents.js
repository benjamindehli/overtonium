/* Marks the section you are currently reading in the contents list.

   The list works without this. Every entry is an ordinary link to an id on
   the same page, so with the script blocked you get a contents list that goes
   where it says, which is what it was before this file existed. All this adds
   is the mark saying which of them you are standing in.

   It finds everything by class and attribute rather than by id, so a page can
   carry a contents list or not and the script neither knows nor cares.

   Reading position is taken from the targets' own rectangles rather than from
   whatever the observer happens to report, because an entry only fires when it
   crosses the line and a page can be scrolled by a whole screen between two
   crossings. The observer is here to say "something moved" cheaply; the answer
   is always computed from scratch. */
(function () {
    "use strict";

    /* How far down the window a heading has to come before it counts as the
       one being read. A line near the top matches where the eye is after a
       jump, since that is exactly where an anchored link leaves the heading. */
    var LINE = 120;

    var nav = document.querySelector(".contents");

    if (!nav) return;

    var outer = nav.querySelector("ul");
    var links = [].slice.call(nav.querySelectorAll('a[href^="#"]'));
    var targets = [];
    var current = null;
    var open = null;

    /* The entry at the top of the tree that this one belongs to, which is the
       list item whose own parent is the outer list. A top-level entry is its
       own. */
    function top(link) {
        var li = link.parentNode;

        while (li && li.parentNode !== outer) {
            li = li.parentNode;
        }

        return li;
    }

    links.forEach(function (link) {
        var el = document.getElementById(link.getAttribute("href").slice(1));

        if (el) targets.push({ el: el, link: link, top: top(link) });
    });

    if (!targets.length) return;

    /* Says that the folding below is running, so the stylesheet only hides a
       section's sub-entries when there is something here to unhide them
       again. With this file blocked the list stays whole. */
    nav.setAttribute("data-folds", "");

    function mark(entry) {
        if (entry === current) return;

        if (current) current.link.removeAttribute("aria-current");

        /* "location" rather than "true", which is the token for the current
           item in a set of links describing where you are. */
        if (entry) entry.link.setAttribute("aria-current", "location");

        current = entry;

        /* And the section it belongs to is the one whose parts are worth
           showing. A page with several sections of parts can list more
           headings than a laptop window has room for, and all but a handful
           of them are about a part of the page you are nowhere near. */
        var wanted = entry ? entry.top : null;

        if (wanted !== open) {
            if (open) open.removeAttribute("data-open");
            if (wanted) wanted.setAttribute("data-open", "");

            open = wanted;
        }
    }

    function update() {
        var found = null;
        var i;

        for (i = 0; i < targets.length; i++) {
            if (targets[i].el.getBoundingClientRect().top > LINE) break;

            found = targets[i];
        }

        /* At the foot of the page nothing more can be scrolled to, so the last
           section is the one being read however short it is. Without this a
           page ending in two brief sections marks neither of them, and the
           rail sits one behind for the whole of the end. */
        if (window.innerHeight + window.scrollY >= document.body.scrollHeight - 2) {
            found = targets[targets.length - 1];
        }

        mark(found || targets[0]);
    }

    /* Coalesced onto a frame. Scroll fires far more often than anything can
       be drawn, and the work below is a rectangle per heading. */
    var waiting = false;

    function schedule() {
        if (waiting) return;

        waiting = true;
        window.requestAnimationFrame(function () {
            waiting = false;
            update();
        });
    }

    if (window.IntersectionObserver) {
        /* The observer is the cheap way to be told that the page has moved
           past a heading. What it reports is ignored: schedule recomputes
           everything, for the reason at the top of this file. */
        var observer = new IntersectionObserver(schedule, {
            rootMargin: "-" + LINE + "px 0px 0px 0px"
        });

        targets.forEach(function (t) {
            observer.observe(t.el);
        });
    }

    /* Still needed with the observer running: it says nothing while a scroll
       stays between two headings, which on the longer sections here is most of
       the scrolling anybody does. */
    window.addEventListener("scroll", schedule, { passive: true });
    window.addEventListener("resize", schedule, { passive: true });

    update();
})();
