; web-browser.scm -- install bookmarks
; Copyright (c) 1997 Misha Dynin <misha@xcf.berkeley.edu>
; Note: script-fu must be able to handle zero argument procedures to
;       process this file!
;
; For more information see webbrowser.readme or
;   http://www.xcf.berkeley.edu/~misha/gimp/
;
; This program is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 2 of the License, or
; (at your option) any later version.
;
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, write to the Free Software
; Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

(set! web-browser-new-window 0)

(define (script-fu-bookmark url)
  (extension-web-browser 1 url web-browser-new-window))

(define (bookmark-register proc menu help)
  (script-fu-register proc menu help
		    "Misha Dynin <misha@xcf.berkeley.edu>"
		    "Misha Dynin"
		    "1997"
		    ""))

(define (script-fu-bookmark-1)
    (script-fu-bookmark "https://github.com/office-42/gimp42"))

(bookmark-register  "script-fu-bookmark-1"
		    "<Toolbox>/Xtns/Web Browser/GIMP42"
		    "Link to https://github.com/office-42/gimp42")

(define (script-fu-bookmark-2)
    (script-fu-bookmark "https://github.com/office-42/gimp42#readme"))

(bookmark-register  "script-fu-bookmark-2"
		    "<Toolbox>/Xtns/Web Browser/Documentation"
		    "Link to https://github.com/office-42/gimp42#readme")

(define (script-fu-bookmark-3)
    (script-fu-bookmark "https://github.com/office-42/gimp42/issues"))

(bookmark-register  "script-fu-bookmark-3"
		    "<Toolbox>/Xtns/Web Browser/Report a Bug"
		    "Link to https://github.com/office-42/gimp42/issues")

(define (script-fu-bookmark-4)
    (script-fu-bookmark "https://github.com/office-42/gimp42/releases"))

(bookmark-register  "script-fu-bookmark-4"
		    "<Toolbox>/Xtns/Web Browser/Download"
		    "Link to https://github.com/office-42/gimp42/releases")

(define (script-fu-bookmark-5)
    (script-fu-bookmark "https://github.com/office-42/gimp42/tree/main"))

(bookmark-register  "script-fu-bookmark-5"
		    "<Toolbox>/Xtns/Web Browser/Source Code"
		    "Link to https://github.com/office-42/gimp42/tree/main")

(define (script-fu-bookmark-6)
    (script-fu-bookmark "https://www.gtk.org/"))

(bookmark-register  "script-fu-bookmark-6"
		    "<Toolbox>/Xtns/Web Browser/GTK"
		    "Link to https://www.gtk.org/")
