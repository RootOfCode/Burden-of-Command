;;;; ================================================================
;;;;  BURDEN OF COMMAND — A WWI Trench Management Tycoon
;;;;  SBCL Common Lisp — Windows/POSIX, no external libraries
;;;;  Run:  sbcl --script boc.lisp
;;;;  Windows Terminal recommended (VT100 + UTF-8 + chcp 65001)
;;;; ================================================================
(in-package :cl-user)
#-win32 (require :sb-posix)

;;; ── Macros ───────────────────────────────────────────────────────

(defmacro clampf (place delta &optional (lo 0) (hi 100))
  "Increment PLACE by DELTA, clamping the result to [LO, HI]."
  `(setf ,place (clamp (+ ,place ,delta) ,lo ,hi)))

(defmacro setf-clamp (place val &optional (lo 0) (hi 100))
  `(setf ,place (clamp ,val ,lo ,hi)))

(defmacro do-squads ((var &optional (idx (gensym "I"))) &body body)
  "Iterate over all active squads. VAR is bound to each squad-s."
  `(loop for ,idx below (gs-squad-count *g*)
         for ,var = (aref (gs-squads *g*) ,idx)
         do ,@body))

(defmacro adjust-all-squads (accessor delta &optional (lo 0) (hi 100))
  "Clamp-increment ACCESSOR of every active squad by DELTA."
  (let ((sq (gensym "SQ")))
    `(do-squads (,sq) (clampf (,accessor ,sq) ,delta ,lo ,hi))))

(defmacro with-color ((&rest spec) &body body)
  "Emit ANSI attrs from SPEC (color constants and/or :bold/:dim), run BODY, reset."
  `(progn
     ,@(loop for s in spec
             collect (case s
                       (:bold '(bold%))
                       (:dim  '(dim%))
                       (t     `(fg% ,s))))
     ,@body
     (rst%)))

(defmacro defenum (prefix &rest names)
  "Define +PREFIX-NAME0+, +PREFIX-NAME1+, ... and +PREFIX-COUNT+ as defconstants."
  (let ((p (string-upcase (symbol-name prefix))))
    `(progn
       ,@(loop for name in names for i from 0
               for sym = (intern (format nil "+~A-~A+" p (string-upcase (symbol-name name)))
                                 :cl-user)
               collect `(defconstant ,sym ,i))
       (defconstant ,(intern (format nil "+~A-COUNT+" p) :cl-user) ,(length names)))))

;;; ── Utilities ────────────────────────────────────────────────────
(defvar *rng* (make-random-state t))

(defun clamp     (x a b) (max a (min b x)))
(defun rng-bool  (p)     (< (random 1.0 *rng*) p))
(defun rng-range (lo hi) (+ lo (random (1+ (- hi lo)) *rng*)))
(defun popcount% (x)     (logcount (ldb (byte 32 0) x)))

(defun make-bar (v mx w)
  (let* ((v (clamp v 0 mx))
         (f (if (zerop mx) 0 (round (* w v) mx))))
    (concatenate 'string
                 (make-string (max 0 f)       :initial-element #\#)
                 (make-string (max 0 (- w f)) :initial-element #\.))))

(defun ppad (s w)
  (format t "~VA" w (if (> (length s) w) (subseq s 0 w) s)))

(defun write-spaces (n) (dotimes (_ n) (write-char #\Space)))
(defun exit-key-p   (k) (or (eq k :esc) (eq k :Q)))

(defun turn->date (turn)
  "Return (values 1-indexed-day 0-indexed-month) for the given game turn."
  (let ((md #(31 28 31 30 31 30 31 31 30 31 30 31))
        (d  (floor (1- turn) 2))
        (mo 0))
    (loop while (and (< mo 11) (>= d (aref md mo)))
          do (decf d (aref md mo)) (incf mo))
    (values (1+ d) mo)))

;;; ── ANSI / Terminal ──────────────────────────────────────────────
(defun at%    (r c) (format t "~C[~D;~DH" #\Escape r c))
(defun cls%   ()    (format t "~C[2J~C[H"  #\Escape #\Escape))
(defun cur-off%()   (format t "~C[?25l" #\Escape))
(defun cur-on% ()   (format t "~C[?25h" #\Escape))
(defun fg%    (c)   (format t "~C[~Dm"  #\Escape c))
(defun bold%  ()    (format t "~C[1m"   #\Escape))
(defun dim%   ()    (format t "~C[2m"   #\Escape))
(defun rst%   ()    (format t "~C[0m"   #\Escape))
(defun flush% ()    (finish-output))

(defconstant +blk+ 30) (defconstant +red+ 31) (defconstant +grn+ 32)
(defconstant +yel+ 33) (defconstant +blu+ 34) (defconstant +mag+ 35)
(defconstant +cyn+ 36) (defconstant +wht+ 37) (defconstant +gry+ 90)

(defparameter +tl+ "╔") (defparameter +tr+ "╗")
(defparameter +bl+ "╚") (defparameter +br+ "╝")
(defparameter +bv+ "║") (defparameter +bh+ "═")
(defparameter +lm+ "╠") (defparameter +rm+ "╣")
(defparameter +tm+ "╦") (defparameter +bm+ "╩")
(defparameter +xx+ "╬") (defparameter +ht+ "─") (defparameter +vt+ "│")
(defparameter +sym-up+ "↑") (defparameter +sym-dn+ "↓")
(defparameter +sym-eq+ "–") (defparameter +sym-lf+ "←") (defparameter +sym-rt+ "→")
(defparameter +sym-bl+ "●") (defparameter +sym-ci+ "○")
(defparameter +sym-hts+ "⌖") (defparameter +sym-ck+ "✓")
(defparameter +sym-wn+ "⚠") (defparameter +sym-sk+ "☠")
(defparameter +sym-tri+ "►")

(defconstant +tw+ 80) (defconstant +div+ 44)
(defconstant +lw+ 42) (defconstant +rw+ 35)

;;; ── Windows Raw Mode ─────────────────────────────────────────────
#+win32
(progn
  (sb-alien:define-alien-routine ("GetStdHandle"       gs-handle%) sb-alien:unsigned-long (n sb-alien:unsigned-long))
  (sb-alien:define-alien-routine ("GetConsoleMode"     gc-mode%)   sb-alien:int (h sb-alien:unsigned-long) (m (* sb-alien:unsigned-long)))
  (sb-alien:define-alien-routine ("SetConsoleMode"     sc-mode%)   sb-alien:int (h sb-alien:unsigned-long) (m sb-alien:unsigned-long))
  (sb-alien:define-alien-routine ("SetConsoleOutputCP" set-out-cp%) sb-alien:int (cp sb-alien:unsigned-int))
  (sb-alien:define-alien-routine ("SetConsoleCP"       set-in-cp%) sb-alien:int (cp sb-alien:unsigned-int))
  (defconstant +std-in+  (ldb (byte 32 0) -10))
  (defconstant +std-out+ (ldb (byte 32 0) -11))
  (defvar *orig-in-mode*  0)
  (defvar *orig-out-mode* 0)
  (defun raw-on ()
    (let ((hi (gs-handle% +std-in+)) (ho (gs-handle% +std-out+)))
      (sb-alien:with-alien ((mi sb-alien:unsigned-long) (mo sb-alien:unsigned-long))
        (gc-mode% hi (sb-alien:addr mi)) (setf *orig-in-mode*  mi)
        (gc-mode% ho (sb-alien:addr mo)) (setf *orig-out-mode* mo)
        (sc-mode% hi #x0081)             ; ENABLE_PROCESSED_INPUT | ENABLE_EXTENDED_FLAGS
        (sc-mode% ho (logior mo #x0005)) ; ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING
        (set-out-cp% 65001) (set-in-cp% 65001))))
  (defun raw-off ()
    (sc-mode% (gs-handle% +std-in+)  *orig-in-mode*)
    (sc-mode% (gs-handle% +std-out+) *orig-out-mode*))

  ;; ReadConsoleInputW reads raw INPUT_RECORD events from the console buffer.
  ;; Unlike ReadFile/ReadConsole, it delivers ALL key events including Enter
  ;; and arrow keys, and is completely unaffected by SBCL's broken (listen).
  ;;
  ;; INPUT_RECORD layout (20 bytes, little-endian):
  ;;   +0   EventType         WORD  (2 bytes)
  ;;   +2   <padding>               (2 bytes, struct alignment)
  ;;   +4   bKeyDown          BOOL  (4 bytes)
  ;;   +8   wRepeatCount      WORD  (2 bytes)
  ;;   +10  wVirtualKeyCode   WORD  (2 bytes)
  ;;   +12  wVirtualScanCode  WORD  (2 bytes)
  ;;   +14  uChar.UnicodeChar WCHAR (2 bytes)
  ;;   +16  dwControlKeyState DWORD (4 bytes)
  (sb-alien:define-alien-routine ("ReadConsoleInputW" rci%)
    sb-alien:int
    (h   sb-alien:unsigned-long)
    (buf (* sb-alien:unsigned-char))
    (len sb-alien:unsigned-long)
    (n   (* sb-alien:unsigned-long)))

  (defconstant +ev-key+ 1)

  (defun win-read-key ()
    ;; Loop discarding non-key events (mouse, resize, focus, etc.) and
    ;; key-up events; return a keyword on the first key-down event.
    (let ((hi (gs-handle% +std-in+)))
      (sb-alien:with-alien ((buf (sb-alien:array sb-alien:unsigned-char 20))
                            (n   sb-alien:unsigned-long))
        (loop
          (rci% hi (sb-alien:cast buf (* sb-alien:unsigned-char)) 1
                (sb-alien:addr n))
          (let ((ev-type (logior (sb-alien:deref buf 0)
                                 (ash (sb-alien:deref buf 1) 8)))
                (key-dn  (logior (sb-alien:deref buf 4)
                                 (ash (sb-alien:deref buf 5) 8)
                                 (ash (sb-alien:deref buf 6) 16)
                                 (ash (sb-alien:deref buf 7) 24)))
                (vk      (logior (sb-alien:deref buf 10)
                                 (ash (sb-alien:deref buf 11) 8)))
                (uc      (logior (sb-alien:deref buf 14)
                                 (ash (sb-alien:deref buf 15) 8))))
            (when (and (= ev-type +ev-key+) (not (zerop key-dn)))
              (return
                (case vk
                  (#x0D :enter)   ; VK_RETURN
                  (#x1B :esc)     ; VK_ESCAPE
                  (#x20 :space)   ; VK_SPACE
                  (#x26 :up)      ; VK_UP
                  (#x28 :down)    ; VK_DOWN
                  (#x25 :left)    ; VK_LEFT
                  (#x27 :right)   ; VK_RIGHT
                  (t (if (and (>= uc 32) (< uc 127))
                         (intern (string (char-upcase (code-char uc))) :keyword)
                         :none))))))))))  ; end win-read-key
  ) ; end #+win32 progn

;;; ── POSIX Raw Mode ───────────────────────────────────────────────
#-win32
(progn
  (defvar *orig-termios* nil)
  (defun raw-on ()
    (setf *orig-termios* (sb-posix:tcgetattr 0))
    (let ((raw (sb-posix:tcgetattr 0)))
      (setf (sb-posix:termios-lflag raw)
            (logandc2 (sb-posix:termios-lflag raw) (logior sb-posix:echo sb-posix:icanon))
            (aref (sb-posix:termios-cc raw) sb-posix:vmin)  1
            (aref (sb-posix:termios-cc raw) sb-posix:vtime) 0)
      (sb-posix:tcsetattr 0 sb-posix:tcsanow raw)))
  (defun raw-off ()
    (when *orig-termios* (sb-posix:tcsetattr 0 sb-posix:tcsanow *orig-termios*))))

;;; ── Key Reading ──────────────────────────────────────────────────
#+win32
(defun read-key ()
  (finish-output)
  (win-read-key))

#-win32
(defun read-key ()
  (finish-output)
  (let ((c (read-char *standard-input* nil :none)))
    (when (eq c :none) (return-from read-key :none))
    (case c
      (#\Escape
       (sleep 0.01)
       (if (listen *standard-input*)
           (let ((c2 (read-char *standard-input* nil nil)))
             (if (and c2 (char= c2 #\[) (listen *standard-input*))
                 (case (read-char *standard-input* nil nil)
                   (#\A :up) (#\B :down) (#\C :right) (#\D :left) (t :none))
                 :esc))
           :esc))
      (#\Return
       ;; Drain a trailing LF if present (handles CR+LF from some terminals).
       (when (listen *standard-input*)
         (let ((nxt (read-char *standard-input* nil nil)))
           (unless (and nxt (char= nxt #\Newline))
             (unread-char nxt *standard-input*))))
       :enter)
      (#\Newline :enter)
      (#\Space   :space)
      (t (cond
           ((and (characterp c) (lower-case-p c)) (intern (string (char-upcase c)) :keyword))
           ((and (characterp c) (upper-case-p c)) (intern (string c) :keyword))
           ((and (characterp c) (char<= #\0 c #\9)) (intern (string c) :keyword))
           (t :none))))))

;;; ── Enum Constants ───────────────────────────────────────────────
(defenum task    standby patrol raid repair rest forage scavenge)
(defenum ration  emergency quarter half full)
(defenum ammo    conserve normal liberal)
(defenum res     food ammo meds tools)
(defenum pers    steadfast brave cowardly drunkard)
(defenum trait   sharpshooter cook medic scrounger musician runner)
(defenum diff    easy normal hard ironman)
(defenum weather clear rain fog snow storm)
(defenum upg     duckboards sandbag dugout periscope lewis-nest sump
                 signal-wire rum-store field-hosp obs-post munitions food-cache)
(defenum cmd     rum letters medical reprimand speech rations-extra
                 ceremony leave treat-wounded supply-req)
(defenum ev      none supply reinforce)
(defenum over    none win lose mutiny)
(defenum revt    artillery enemy-raid gas mail rats influenza sgt-breakdown
                 supply-convoy reinforce sniper fraternize hero friendly-fire
                 cache food-spoil ammo-damp wound-heal sector-assault)

;;; ── Data Tables ──────────────────────────────────────────────────
(defstruct (td (:conc-name td-)) name color fat-delta mor-delta ammo-cost tools-gain food-gain ammo-gain desc)
(defparameter +task-defs+
  (vector
   (make-td :name "STANDBY"  :color +wht+ :fat-delta  -5 :mor-delta  0 :ammo-cost 1 :tools-gain 0 :food-gain 0 :ammo-gain 0 :desc "Hold position. Conserves fatigue.")
   (make-td :name "PATROL"   :color +cyn+ :fat-delta +10 :mor-delta +3 :ammo-cost 3 :tools-gain 0 :food-gain 0 :ammo-gain 0 :desc "Scout no-man's-land. Morale up, costs ammo.")
   (make-td :name "RAID"     :color +red+ :fat-delta +20 :mor-delta +4 :ammo-cost 6 :tools-gain 0 :food-gain 0 :ammo-gain 0 :desc "Strike enemy lines. High risk, high reward.")
   (make-td :name "REPAIR"   :color +yel+ :fat-delta  +5 :mor-delta  0 :ammo-cost 1 :tools-gain 2 :food-gain 0 :ammo-gain 0 :desc "Fix works. Generates tools each turn.")
   (make-td :name "REST"     :color +grn+ :fat-delta -15 :mor-delta +5 :ammo-cost 1 :tools-gain 0 :food-gain 0 :ammo-gain 0 :desc "Stand down. Restores fatigue and morale.")
   (make-td :name "FORAGE"   :color +mag+ :fat-delta +12 :mor-delta -2 :ammo-cost 0 :tools-gain 0 :food-gain 6 :ammo-gain 0 :desc "Scour billets and farms. Find food; low morale.")
   (make-td :name "SCAVENGE" :color +yel+ :fat-delta +15 :mor-delta -3 :ammo-cost 0 :tools-gain 1 :food-gain 0 :ammo-gain 3 :desc "Strip no-man's-land for materiel. Find ammo+tools.")))
(defparameter +order-opts+ #(0 1 2 3 4 5 6))

(defstruct (rd (:conc-name rd-)) name color food-mul mor-per-turn desc)
(defparameter +ration-defs+
  (vector
   (make-rd :name "EMERGENCY" :color +red+ :food-mul 0.30 :mor-per-turn -8 :desc "Bare minimum. Severe morale drain. Last resort.")
   (make-rd :name "QUARTER"   :color +red+ :food-mul 0.55 :mor-per-turn -4 :desc "Quarter rations. Significant morale impact.")
   (make-rd :name "HALF"      :color +yel+ :food-mul 0.75 :mor-per-turn -1 :desc "Half rations. Minor morale cost. Stretches supply.")
   (make-rd :name "FULL"      :color +grn+ :food-mul 1.00 :mor-per-turn  0 :desc "Full issue. No morale penalty. Standard rate.")))

(defstruct (ad (:conc-name ad-)) name color ammo-mul patrol-mor-mul raid-resist-add desc)
(defparameter +ammo-defs+
  (vector
   (make-ad :name "CONSERVE" :color +grn+ :ammo-mul 0.60 :patrol-mor-mul 0.60 :raid-resist-add -0.10 :desc "Save ammo. Patrols less effective. Harder to hold raids.")
   (make-ad :name "NORMAL"   :color +wht+ :ammo-mul 1.00 :patrol-mor-mul 1.00 :raid-resist-add  0.00 :desc "Standard issue. Balanced effectiveness.")
   (make-ad :name "LIBERAL"  :color +red+ :ammo-mul 1.50 :patrol-mor-mul 1.40 :raid-resist-add  0.15 :desc "Spend freely. Maximum patrol/raid effectiveness.")))

(defstruct (br (:conc-name br-)) from to give get desc)
(defparameter +barter-rates+
  (vector
   (make-br :from +res-food+  :to +res-ammo+  :give 20 :get  8 :desc "Trade rations for rounds (20 food -> 8 ammo)")
   (make-br :from +res-food+  :to +res-meds+  :give 15 :get  5 :desc "Barter food for field dressings (15 food -> 5 meds)")
   (make-br :from +res-food+  :to +res-tools+ :give 18 :get  4 :desc "Exchange rations for tools (18 food -> 4 tools)")
   (make-br :from +res-ammo+  :to +res-food+  :give  8 :get 18 :desc "Sell rounds for food (8 ammo -> 18 food)")
   (make-br :from +res-ammo+  :to +res-meds+  :give 10 :get  4 :desc "Swap bullets for bandages (10 ammo -> 4 meds)")
   (make-br :from +res-ammo+  :to +res-tools+ :give  8 :get  4 :desc "Trade shells for equipment (8 ammo -> 4 tools)")
   (make-br :from +res-meds+  :to +res-food+  :give  5 :get 14 :desc "Sell dressings for rations (5 meds -> 14 food)")
   (make-br :from +res-meds+  :to +res-ammo+  :give  4 :get  9 :desc "Exchange morphia for rounds (4 meds -> 9 ammo)")
   (make-br :from +res-meds+  :to +res-tools+ :give  3 :get  4 :desc "Trade supplies for tools (3 meds -> 4 tools)")
   (make-br :from +res-tools+ :to +res-food+  :give  3 :get 14 :desc "Sell tools for food (3 tools -> 14 food)")
   (make-br :from +res-tools+ :to +res-ammo+  :give  3 :get  7 :desc "Trade equipment for ammo (3 tools -> 7 ammo)")
   (make-br :from +res-tools+ :to +res-meds+  :give  4 :get  3 :desc "Exchange tools for field kit (4 tools -> 3 meds)")))
(defparameter +res-names+  #("Food" "Ammo" "Meds" "Tools"))
(defparameter +res-colors+ (vector +grn+ +yel+ +cyn+ +mag+))

(defstruct (pd (:conc-name pd-)) name mul effect-str)
(defparameter +pers-defs+
  (vector (make-pd :name "Steadfast" :mul 1.00 :effect-str "Consistent and reliable.")
          (make-pd :name "Brave"     :mul 1.20 :effect-str "+20% to fatigue/morale changes.")
          (make-pd :name "Cowardly"  :mul 0.70 :effect-str "-30% effectiveness under fire.")
          (make-pd :name "Drunkard"  :mul 0.85 :effect-str "-15% effectiveness; unpredictable.")))

(defstruct (trd (:conc-name trd-)) name color effect res-id res-amt)
(defparameter +trait-defs+
  (vector
   (make-trd :name "Sharpshooter" :color +yel+ :effect "Raid resist +10%. Sniper chance halved." :res-id -1 :res-amt 0)
   (make-trd :name "Cook"         :color +grn+ :effect "Saves 1 food per turn passively."         :res-id +res-food+ :res-amt 1)
   (make-trd :name "Medic"        :color +cyn+ :effect "Treats 1 wound per turn automatically."   :res-id +res-meds+ :res-amt 0)
   (make-trd :name "Scrounger"    :color +mag+ :effect "Finds 1 ammo or tools per turn randomly." :res-id +res-ammo+ :res-amt 1)
   (make-trd :name "Musician"     :color +wht+ :effect "Morale +2 per turn passively."            :res-id -1 :res-amt 0)
   (make-trd :name "Runner"       :color +blu+ :effect "Convoy ETA reduced by 1 turn."            :res-id -1 :res-amt 0)))

(defstruct (dd (:conc-name dd-)) name subtitle color food-mul event-mul morale-mul score-mul-x10 save-allowed)
(defparameter +diff-defs+
  (vector
   (make-dd :name "GREEN FIELDS"  :subtitle "Resources plentiful. Events rare.     For newcomers." :color +grn+ :food-mul 0.6 :event-mul 0.6 :morale-mul 0.7 :score-mul-x10  8 :save-allowed t)
   (make-dd :name "INTO THE MUD"  :subtitle "Balanced. The intended experience.    Recommended."   :color +yel+ :food-mul 1.0 :event-mul 1.0 :morale-mul 1.0 :score-mul-x10 10 :save-allowed t)
   (make-dd :name "NO MAN'S LAND" :subtitle "Scarce supplies. Brutal events.       For veterans."  :color +red+ :food-mul 1.4 :event-mul 1.5 :morale-mul 1.3 :score-mul-x10 14 :save-allowed t)
   (make-dd :name "GOD HELP US"   :subtitle "One life. No loading. Maximum stakes. True iron."     :color +mag+ :food-mul 1.6 :event-mul 1.8 :morale-mul 1.5 :score-mul-x10 20 :save-allowed nil)))

(defstruct (wd  (:conc-name wd-))  label color)
(defstruct (wfx (:conc-name wfx-)) fat-per-turn food-extra agg-drift raid-mul note)
(defparameter +weather-defs+
  (vector (make-wd :label "Clear  " :color +cyn+) (make-wd :label "Rainy  " :color +blu+)
          (make-wd :label "Foggy  " :color +wht+) (make-wd :label "Snowing" :color +wht+)
          (make-wd :label "Storm  " :color +mag+)))
(defparameter +weather-fx+
  (vector
   (make-wfx :fat-per-turn 0 :food-extra 0 :agg-drift  0 :raid-mul 1.00 :note "No effect on operations.")
   (make-wfx :fat-per-turn 3 :food-extra 1 :agg-drift  0 :raid-mul 1.00 :note "Mud slows everything. +fatigue, +food.")
   (make-wfx :fat-per-turn 1 :food-extra 0 :agg-drift  0 :raid-mul 1.50 :note "Low visibility. Raids far more likely.")
   (make-wfx :fat-per-turn 4 :food-extra 2 :agg-drift +2 :raid-mul 0.75 :note "Bitter cold. Heavy fatigue and food drain.")
   (make-wfx :fat-per-turn 6 :food-extra 1 :agg-drift +4 :raid-mul 1.20 :note "Storm. Severe fatigue. Enemy emboldened.")))

(defun weather-next (w)
  (aref (vector w w w w +weather-rain+ +weather-rain+ +weather-fog+
                +weather-clear+ +weather-storm+ +weather-clear+)
        (rng-range 0 9)))

(defun mor-label (m)
  (cond ((>= m 80) "EXCELLENT") ((>= m 65) "GOOD") ((>= m 45) "FAIR")
        ((>= m 25) "POOR") (t "CRITICAL")))
(defun mor-color (m) (if (>= m 65) +grn+ (if (>= m 45) +yel+ +red+)))

(defparameter +raid-resist+ #(0.30 0.55 0.80 0.40 0.10 0.20 0.35))
(defparameter +rand-probs+
  #((0.0 350.0)(0.0 500.0)(0.035 0.0)(0.10 0.0)(0.07 0.0)
    (0.04 0.0)(0.03 0.0)(0.08 0.0)(0.04 0.0)
    (0.0 600.0)(0.012 0.0)(0.05 0.0)(0.008 0.0)(0.0 700.0)
    (0.06 0.0)(0.04 0.0)(0.12 0.0)(0.0 450.0)))

(defstruct (ud (:conc-name ud-)) name tools-cost desc passive)
(defparameter +upg-defs+
  (vector
   (make-ud :name "Duckboards"         :tools-cost  8 :desc "Raised walkways over the mud."         :passive "Rain/storm fatigue halved.")
   (make-ud :name "Sandbag Revetments" :tools-cost 12 :desc "Reinforced firing bay."                :passive "Artillery casualties -1.")
   (make-ud :name "Officers' Dugout"   :tools-cost 15 :desc "Reinforced command shelter."           :passive "+1 morale/turn all squads passively.")
   (make-ud :name "Periscope"          :tools-cost 10 :desc "Mirror periscope for safe obs."        :passive "PATROL +2 extra morale/turn.")
   (make-ud :name "Lewis Gun Nest"     :tools-cost 20 :desc "Sandbagged MG emplacement."            :passive "+15% raid resistance all squads.")
   (make-ud :name "Trench Sump"        :tools-cost  6 :desc "Drainage channel under fire-step."     :passive "Disease/influenza chance halved.")
   (make-ud :name "Signal Wire"        :tools-cost  8 :desc "Buried telephone line to support."     :passive "+1 CP per AM turn.")
   (make-ud :name "Rum Store"          :tools-cost 10 :desc "Locked medicinal rum crate."           :passive "Rum Ration costs 0 CP.")
   (make-ud :name "Field Hospital"     :tools-cost 18 :desc "Expanded aid post with stretchers."    :passive "Wounds heal 1 extra/turn. Meds halved on triage.")
   (make-ud :name "Observation Post"   :tools-cost 14 :desc "Forward observation point."            :passive "Sniper/raid events -25% chance.")
   (make-ud :name "Munitions Store"    :tools-cost 12 :desc "Reinforced ammo crypt."                :passive "Ammo max +25. Ammo Damp events eliminated.")
   (make-ud :name "Food Cache"         :tools-cost 10 :desc "Hidden emergency food reserve."        :passive "Food max +25. Spoilage events eliminated.")))
(defparameter +upg-shorts+ #("DB" "SB" "DG" "PR" "LG" "SU" "SW" "RS" "FH" "OP" "MN" "FC"))

(defstruct (cd (:conc-name cd-)) name cp-cost food-cost meds-cost desc effect)
(defparameter +cmd-defs+
  (vector
   (make-cd :name "Rum Ration"        :cp-cost 1 :food-cost  5 :meds-cost 0 :desc "Issue rum to selected squad."        :effect "+15 morale selected squad. -5 food.")
   (make-cd :name "Write Letters"     :cp-cost 1 :food-cost  0 :meds-cost 0 :desc "Help men write letters home."         :effect "+10 morale selected squad.")
   (make-cd :name "Medical Triage"    :cp-cost 1 :food-cost  0 :meds-cost 5 :desc "Field dressing rotation."             :effect "-25 fatigue selected squad. -5 meds.")
   (make-cd :name "Inspect/Reprimand" :cp-cost 0 :food-cost  0 :meds-cost 0 :desc "Snap inspection."                     :effect "-8 fatigue, -5 morale. No CP cost.")
   (make-cd :name "Officer's Speech"  :cp-cost 2 :food-cost  0 :meds-cost 0 :desc "Address entire company."              :effect "+8 morale all squads.")
   (make-cd :name "Emergency Rations" :cp-cost 2 :food-cost 20 :meds-cost 0 :desc "Break emergency food."                :effect "-15 fatigue all squads. -20 food.")
   (make-cd :name "Medal Ceremony"    :cp-cost 3 :food-cost  0 :meds-cost 0 :desc "Formal commendation at HQ."           :effect "+20 morale selected squad. +1 medal.")
   (make-cd :name "Comp. Leave"       :cp-cost 2 :food-cost  0 :meds-cost 0 :desc "Send one man to rear."                :effect "-1 man but +15 morale. Rare mercy.")
   (make-cd :name "Treat Wounded"     :cp-cost 1 :food-cost  0 :meds-cost 3 :desc "Emergency wound treatment."           :effect "Heal up to 2 wounds in selected squad. -3 meds.")
   (make-cd :name "Supply Request"    :cp-cost 2 :food-cost  0 :meds-cost 0 :desc "Petition HQ for supplies."            :effect "HQ rep-dependent convoy in 5-10 turns.")))

(defstruct (hd (:conc-name hd-))
  turn title body comply-label defy-label comply-result defy-result
  cy-agg cy-all-mor cy-ammo cy-food cy-force-raid cy-all-standby cy-lose-men cy-medals
  df-agg df-all-mor cy-rep-delta df-rep-delta)
(defparameter +hq-dispatches+
  (vector
   (make-hd :turn 5  :title "BRIGADE ORDER - MANDATORY NIGHT RAID"
    :body '("Brigade HQ requires one section to advance at 0300 and destroy"
            "the enemy wire-cutting position at grid ref C-4."
            "Capture of prisoners would be desirable but not required."
            "Failure to execute this order will be viewed most seriously."
            "Acknowledge receipt by runner. Captain Thorne to confirm personally.")
    :comply-label "COMPLY - Assign a squad to raid" :defy-label "DEFY  - Decline the order"
    :comply-result "Raid launched. Intel noted by Brigade. Aggression falls."
    :defy-result "Brigade marks you 'obstructive'. Enemy grows bolder."
    :cy-agg -8 :cy-all-mor 0 :cy-ammo -10 :cy-food 0 :cy-force-raid t :cy-all-standby nil :cy-lose-men 0 :cy-medals 0
    :df-agg +14 :df-all-mor -5 :cy-rep-delta +10 :df-rep-delta -15)
   (make-hd :turn 16 :title "HQ ORDER - RATION REDUCTION EFFECTIVE IMMEDIATELY"
    :body '("Supply lines between the railhead and the forward position"
            "are under sustained enemy interdiction fire."
            "All forward units are to reduce daily ration draw by one quarter,"
            "effective at 0600 tomorrow." "The men are expected to bear this with good grace.")
    :comply-label "COMPLY - Reduce rations" :defy-label "DEFY  - Maintain full rations"
    :comply-result "Rations cut. Food reserves fall sharply. Men are hungry but obedient."
    :defy-result "HQ notes your refusal. Next convoy will be delayed."
    :cy-agg 0 :cy-all-mor -3 :cy-ammo -5 :cy-food -25 :cy-force-raid nil :cy-all-standby nil :cy-lose-men 0 :cy-medals 0
    :df-agg +8 :df-all-mor 0 :cy-rep-delta +5 :df-rep-delta -20)
   (make-hd :turn 28 :title "ORDER - SECONDMENT TO 3RD PIONEER BATTALION"
    :body '("The Pioneer Corps requires experienced infantry for deep tunneling"
            "operations in the Messines sector. Two men from your company"
            "are to report to 3rd Pioneer Bn HQ by 0600 hours tomorrow."
            "Selection is at the Company Commander's discretion."
            "This is not a request. Acknowledge by 2200 tonight.")
    :comply-label "COMPLY - Send two men" :defy-label "DEFY  - Refuse the transfer"
    :comply-result "Two men depart. Pioneers reciprocate with ammunition."
    :defy-result "You protect your men. Brigade is furious. Enemy senses weakness."
    :cy-agg 0 :cy-all-mor -4 :cy-ammo +8 :cy-food 0 :cy-force-raid nil :cy-all-standby nil :cy-lose-men 2 :cy-medals 0
    :df-agg +12 :df-all-mor -6 :cy-rep-delta +8 :df-rep-delta -18)
   (make-hd :turn 44 :title "INTELLIGENCE DISPATCH - GERMAN OFFENSIVE EXPECTED"
    :body '("Brigade Intelligence reports German forces massing along a"
            "3-kilometre front including your sector."
            "All units are to assume defensive posture immediately."
            "Ammunition is being pre-positioned at forward dumps."
            "All sections to STANDBY. Conserve all resources. Acknowledge.")
    :comply-label "COMPLY - Stand all squads by" :defy-label "DEFY  - Maintain current posture"
    :comply-result "All squads to STANDBY. Ammo delivered. Enemy can't find an opening."
    :defy-result "Men prefer their tasks. Offensive hits a disorganised sector."
    :cy-agg +5 :cy-all-mor 0 :cy-ammo +20 :cy-food 0 :cy-force-raid nil :cy-all-standby t :cy-lose-men 0 :cy-medals 0
    :df-agg +10 :df-all-mor +5 :cy-rep-delta +12 :df-rep-delta -8)
   (make-hd :turn 60 :title "COMMENDATION - VICTORIA CROSS NOMINATION REQUESTED"
    :body '("Brigade Commander writes personally: the conduct of the men"
            "of this sector has been noted with admiration at Corps level."
            "One man is to be forwarded for formal commendation."
            "Submit name by runner at first light."
            "This is an honour reflecting upon the whole company.")
    :comply-label "COMPLY - Submit a nomination" :defy-label "DEFY  - Decline the honour"
    :comply-result "Ceremony held at Brigade. The men are enormously proud."
    :defy-result "Word spreads the Captain blocked the honour. Morale suffers."
    :cy-agg 0 :cy-all-mor +10 :cy-ammo 0 :cy-food 0 :cy-force-raid nil :cy-all-standby nil :cy-lose-men 0 :cy-medals 2
    :df-agg 0 :df-all-mor -5 :cy-rep-delta +15 :df-rep-delta -12)))

(defstruct (he (:conc-name he-)) turn text agg-delta all-mor-delta all-fat-delta set-weather)
(defparameter +hist-events+
  (vector
   (make-he :turn  2 :text "HQ runner: 'Hold sector at all costs. No retreat. Acknowledge.'"    :agg-delta +5 :all-mor-delta  0 :all-fat-delta  0 :set-weather -1)
   (make-he :turn  7 :text "End of first week. The rain has arrived. The mud is inescapable."   :agg-delta +3 :all-mor-delta -3 :all-fat-delta +5 :set-weather +weather-rain+)
   (make-he :turn 14 :text "A chaplain visits the line. He is nineteen, fresh from seminary."   :agg-delta  0 :all-mor-delta +4 :all-fat-delta  0 :set-weather -1)
   (make-he :turn 21 :text "Haig communique: 'continued pressure'. The men find this darkly amusing." :agg-delta 0 :all-mor-delta 0 :all-fat-delta 0 :set-weather -1)
   (make-he :turn 28 :text "A German deserter is escorted through your sector. He is sixteen."  :agg-delta -5 :all-mor-delta +2 :all-fat-delta  0 :set-weather -1)
   (make-he :turn 35 :text "Six days of rain. The mud has become the primary tactical obstacle." :agg-delta  0 :all-mor-delta -3 :all-fat-delta +6 :set-weather +weather-rain+)
   (make-he :turn 42 :text "Half the campaign. A relief column passes -- they are not for you."  :agg-delta +5 :all-mor-delta -5 :all-fat-delta  0 :set-weather -1)
   (make-he :turn 49 :text "Word: a major offensive has failed eight miles north."               :agg-delta +8 :all-mor-delta -4 :all-fat-delta  0 :set-weather -1)
   (make-he :turn 56 :text "Rumour: peace negotiations. No one believes it. Everyone wants to." :agg-delta  0 :all-mor-delta +5 :all-fat-delta  0 :set-weather -1)
   (make-he :turn 63 :text "The men have stopped flinching at close shell-bursts. A bad sign."  :agg-delta +3 :all-mor-delta  0 :all-fat-delta  0 :set-weather -1)
   (make-he :turn 70 :text "Final week. The men are lean, hollow-eyed, and unbreakable."        :agg-delta  0 :all-mor-delta +6 :all-fat-delta -5 :set-weather -1)
   (make-he :turn 77 :text "Relief units spotted 3 miles behind the line. Almost."              :agg-delta  0 :all-mor-delta +8 :all-fat-delta -8 :set-weather -1)))

(defstruct (ce (:conc-name ce-)) title title-color lines)
(defparameter +codex+
  (vector
   (make-ce :title "THE WESTERN FRONT" :title-color +yel+ :lines '("The Western Front stretches 700 km from the North Sea to Switzerland." "Since the failure of the Schlieffen Plan in 1914, both sides entrenched." "Progress is measured in metres and paid for in thousands of lives." "You command a sector near Passchendaele, Flanders, Belgium." "The mud here is legendary. Men have drowned in shell craters." "Your orders arrive from Brigade HQ twelve miles behind the line." "They have not personally visited the front in three months." "The Somme claimed 57,000 British casualties on its first day alone." "The Third Ypres offensive (Passchendaele) began July 31st, 1917." "Haig believes attrition will break Germany. The men are not consulted."))
   (make-ce :title "TRENCH WARFARE" :title-color +cyn+ :lines '("The front-line trench: a ditch six feet deep and two feet wide." "Duckboards line the floor. Walls shored with corrugated iron." "A fire-step lets men peer over the parapet at stand-to." "Behind: support trenches, reserve trenches, communication lines." "The smell: mud, cordite, decay, latrine, wet wool. Indescribable." "Trench foot afflicts men standing in water without relief." "Rats as large as cats devour rations and gnaw at the sleeping." "Average life expectancy of a second lieutenant: six weeks." "Between attacks: fatigues, carrying parties, digging, sandbagging." "A man can go mad from the waiting as easily as from the shells."))
   (make-ce :title "GAS WARFARE" :title-color +grn+ :lines '("April 1915: the Germans first used chlorine gas at Ypres." "The yellow-green cloud drifted over Allied trenches at dusk." "Men described it as lungs full of fire -- drowning from inside." "Phosgene followed: colourless, smelling faintly of cut hay. Deadlier." "Mustard gas, introduced July 1917, blisters skin and blinds eyes." "Mustard gas has no immediate smell. Men often don't know until too late." "A well-drilled unit with Small Box Respirators can survive most attacks." "Without medical supplies a gas attack leaves lasting casualties." "Gas masks fog in cold air and suffocate under exertion." "The sound of the gas alarm -- phosgene rattles -- haunts veterans for life."))
   (make-ce :title "CAPTAIN ALISTAIR THORNE" :title-color +yel+ :lines '("Captain Alistair Thorne, age 29. 11th Battalion, East Lancashire Regiment." "Born in Preston. Joined Kitchener's New Army voluntarily in August 1914." "Commissioned after the Somme. Three of his original platoon survived." "He does not speak of the Somme. He does not need to." "He writes to his mother every Sunday. Half the letters are returned." "He was mentioned in dispatches once, at Beaumont Wood." "He keeps a photograph of his sister Agnes inside his breast pocket." "His wristwatch stopped during a barrage in April. He has not replaced it." "He still believes in what he is doing. He is no longer certain why." "He is, in all practical terms, the last man standing in his draft."))
   (make-ce :title "SGT. HARRIS -- BRAVE" :title-color +red+ :lines '("Sergeant Thomas Harris, age 34. Former coal miner from Wigan." "Volunteered August 1914, driven by genuine and uncomplicated patriotism." "Was at the Gallipoli landings. Does not discuss what he saw there." "His bravery borders on recklessness -- volunteers for every night raid." "The men of Alpha Section follow him without hesitation." "He would die for any one of them. They all know it." "A recommendation for the Military Medal is pending at Brigade." "He sharpens his bayonet each evening. He reads no books." "His hands tremble slightly at breakfast. He pours his tea very carefully." "He does not mention the trembling. Neither do the men."))
   (make-ce :title "SGT. MOORE -- STEADFAST" :title-color +grn+ :lines '("Sergeant William Moore, age 41. Regular Army since 1897." "Fought in the Second Boer War, the Northwest Frontier, France since 1914." "The most experienced soldier in the company by a considerable margin." "He is the calm eye at the centre of every storm." "Even under the heaviest barrage he moves deliberately, checking men." "He has four daughters at home in Dorset. He writes each weekly." "He has never been wounded. The men consider this extraordinary luck." "His vice: strong tea, guarded jealously from all appropriation." "He has seen enough officers come and go that he does not form attachments." "He respects Captain Thorne. This is rare and means a great deal."))
   (make-ce :title "SGT. LEWIS -- DRUNKARD" :title-color +yel+ :lines '("Sergeant Owen Lewis, age 38. Former schoolteacher from Cardiff, Wales." "Joined 1915, commissioned briefly as a second lieutenant." "Demoted following an incident involving a rum store and a court of inquiry." "The incident does not appear in any accessible official record." "He was a fine soldier and a finer man once. The rum ration found him." "He hides bottles inside the sandbag walls. The men pretend not to notice." "On good mornings he recites Keats and Wilfred Owen to the section." "The men listen. They do not always understand. They listen anyway." "He is terrified of dying and more terrified that he deserves to." "He has not had a full night's sleep since the winter of 1915."))
   (make-ce :title "SGT. BELL -- COWARDLY" :title-color +mag+ :lines '("Sergeant Arthur Bell, age 26. Bank clerk from Lambeth, London." "Conscripted under the Military Service Act in January 1916." "He did not want to come. He stated this clearly to his tribunal." "His sergeant's stripes came after two better men died in one week." "He freezes under concentrated fire. His men have begun to notice." "He is not a villain. He is a man violently miscast by history." "Privately he writes detailed letters cataloguing his fear." "He sends them to no one. He keeps them in a tin under his bedroll." "He prays every night. He is no longer certain anyone is listening." "He would be good at his former job. He was very good at his former job."))
   (make-ce :title "THE ENEMY -- IMPERIAL GERMAN ARMY" :title-color +red+ :lines '("The German Imperial Army: professional, adaptive, formidably supplied." "Their Sturmtruppen (stormtrooper) tactics, developed in 1917, are new." "Infiltration: small groups bypass strongpoints to strike the rear." "German artillery is heavy, accurate, and seemingly inexhaustible." "You rarely see them. They are voices in darkness and metal in daylight." "Some prisoners are boys of sixteen. The war does not discriminate." "German soldiers call no-man's-land 'Niemandsland'. The word is the same." "The German soldier opposite has the same rations, same rats," "same fear. His officers told him the same lies. He holds his line." "This does not make him your enemy any less. It makes it worse."))
   (make-ce :title "MEDICAL CARE IN THE FIELD" :title-color +cyn+ :lines '("Regimental Aid Post: first stop for casualties. One Medical Officer." "Stretcher-bearers carry wounded through open ground under fire." "Casualty Clearing Stations are five miles back -- hours away." "Shell shock (neurasthenia) is poorly understood. Most are sent back." "Morphia is scarce. Many receive rum, a pad, and a quiet word." "Your medical supplies: dressings, anti-gas equipment, morphia." "Run out and gas attacks become catastrophic. Disease spreads." "Trench foot: preventable with dry socks and foot inspections." "Wounds left untreated become infected. Men die in days." "Every med spent is a bet that the man is worth saving. He is."))
   (make-ce :title "WEAPONS & MATERIEL" :title-color +yel+ :lines '("Lee-Enfield Mk III: 15 rounds per minute in trained hands." "Pattern 1907 bayonet: 17 inches of steel. The last resort. Used often." "Mills bomb No. 5: essential for raids and close defence." "Lewis gun: light machine gun, 47-round drum. Section's backbone." "Stokes mortar: short range, high arc. Extraordinarily effective." "18-pounder field gun: workhorse of British artillery support." "Ammo represents rounds, bombs, and mortar shells combined." "Running low costs patrols their effectiveness. Men feel naked." "LIBERAL ammo policy maximises combat power. Budget carefully." "CONSERVE policy stretches supply but weakens every engagement."))
   (make-ce :title "RATIONS & SUPPLY" :title-color +grn+ :lines '("A British soldier required 3,000 calories per day in the field." "Bully beef (canned corned beef) and hard tack biscuit were standard." "Tea was not optional. It was as important as ammunition." "Food on HALF rations produces quietly corrosive morale damage." "EMERGENCY rations produce open resentment within 48 hours." "The rum ration (SRD -- Services Rum Diluted) was issued at stand-to." "Issued at the captain's discretion. It was always deeply appreciated." "Rats attacked food stores nightly. Eternal vigilance was required." "A good cook (Pte. Whitmore notwithstanding) was worth two riflemen." "The men could endure almost anything if they were fed."))
   (make-ce :title "COMMAND & THE OFFICER'S BURDEN" :title-color +cyn+ :lines '("The company commander stands between men and a distant, abstract war." "His orders come from men who have not seen the front in months." "His decisions -- who raids, who rests, who is sacrificed -- are real." "Command Points represent the finite reserve of personal authority." "A captain can inspire, cajole, comfort, threaten -- but not endlessly." "Issue rum. Write letters for the illiterate. Hold a medal ceremony." "Every act of personal leadership costs something from the commander." "The men do not need a general. They need a captain who knows their names." "Thorne knows all of them. He knows their wives' names. Their children's." "He will carry that weight whether they survive or not."))
   (make-ce :title "TRENCH ENGINEERING" :title-color +mag+ :lines '("The British soldier spent far more time digging than fighting." "Duckboards, drainage sumps, fire-steps: each built by hand and bayonet." "Sandbag revetments absorb shell fragments and reduce casualties." "A well-built dugout provides shelter and raises spirits." "The Lewis gun nest: a sandbagged emplacement for the section weapon." "Signal wire buried in the floor allowed communication under fire." "Trench periscopes -- mirrors on a stick -- let men observe safely." "The Field Hospital upgrade doubles the value of every medical supply." "The Observation Post reveals threats before they strike." "Build early. Build well. The mud will take everything else."))
   (make-ce :title "NOTABLE MEN -- THE RANK AND FILE" :title-color +wht+ :lines '("Behind every statistic is a man with a name." "Pte. Whitmore is a crack shot. He volunteers for sniping duty." "Beecroft keeps a battered recipe book and makes rations edible." "Pte. Morley learned first aid from his mother. He uses it every night." "Darton plays mouth organ after stand-to. The men are grateful." "Pollard can find a use for anything. His pouches are always full." "Stubbs runs messages faster than anyone alive. He is also terrified." "Colby barters cigarettes for rations with the supply wagoners." "Finch shot a German officer at 400 yards in the fog. He does not boast." "These men did not choose this. They chose each other. That is enough."))))

;;; ── Game Structures ──────────────────────────────────────────────
(defconstant +max-squads+    8)
(defconstant +max-msgs+      7)
(defconstant +max-evq+      16)
(defconstant +max-diary+   192)
(defconstant +msg-len+      72)
(defconstant +res-hist-len+ 10)
(defconstant +max-notables+  2)

(defstruct (sgt-s     (:conc-name ss-)) (name "" :type string) (pers 0) ok)
(defstruct (notable-s (:conc-name ns-)) (name "" :type string) (trait 0) alive)
(defstruct (squad-s   (:conc-name sq-))
  (name "" :type string) (men 0) (maxm 0) (mor 0) (fat 0) (sick 0)
  (task 0) has-sgt (sgt (make-sgt-s)) (lore-idx 0) (wounds 0)
  (notables (make-array +max-notables+ :initial-element (make-notable-s)))
  (notable-count 0) (raids-repelled 0) (men-lost 0) (turns-alive 0))
(defstruct (sev-s (:conc-name ev-)) (at 0) (type 0) (food 0) (ammo 0) (meds 0) (tools 0) (men 0))
(defstruct (de-s  (:conc-name de-)) (turn 0) (half-am 0) (text "" :type string))

(defstruct (gs (:conc-name gs-))
  (turn 1) (maxt 84) am
  (weather 0) (agg 40)
  (food 80) (ammo 60) (meds 30) (tools 40)
  (ration-level +ration-full+) (ammo-policy +ammo-normal+)
  (sector-threat (make-array 4 :element-type 'fixnum :initial-element 30))
  (squads (make-array +max-squads+ :initial-element (make-squad-s))) (squad-count 0)
  (msgs (make-array +max-msgs+ :element-type 'string :initial-contents (loop repeat +max-msgs+ collect "")))
  (msg-count 0)
  (diary (make-array +max-diary+ :initial-element (make-de-s))) (diary-count 0)
  (evq   (make-array +max-evq+   :initial-element (make-sev-s))) (ev-count 0)
  (hist-fired 0)
  dispatch-pending (dispatch-done 0)
  (convoy-delayed 0) (hq-rep 50) supply-req-pending
  (res-hist (make-array (list +res-hist-len+ +res-count+) :element-type 'fixnum :initial-element 0))
  (res-hist-count 0)
  (sel 0) orders-mode (osel 0)
  (cmd-points 2) (upgrades 0)
  (difficulty +diff-normal+)
  (score 0) (medals 0) (forced-standby-turns 0)
  (over +over-none+))

(defvar *g* (make-gs))

;;; ── Save / Load ──────────────────────────────────────────────────
(defconstant +save-magic+   #xB0C1917)
(defconstant +save-version+ 6)
(defconstant +save-slots+   3)
(defparameter +save-names+ #("boc_s0.dat" "boc_s1.dat" "boc_s2.dat"))

(defun save-slot (slot)
  (handler-case
      (with-open-file (f (aref +save-names+ slot) :direction :output
                                                   :if-exists :supersede
                                                   :external-format :utf-8)
        (with-standard-io-syntax
          (let ((*print-readably* t) (*print-circle* t))
            (write (list +save-magic+ +save-version+ *g*) :stream f)))
        t)
    (error () nil)))

(defun load-slot (slot)
  (handler-case
      (with-open-file (f (aref +save-names+ slot) :direction :input :external-format :utf-8)
        (with-standard-io-syntax
          (let ((data (read f nil nil)))
            (when (and data
                       (= (first  data) +save-magic+)
                       (= (second data) +save-version+))
              (setf *g* (third data)) t))))
    (error () nil)))

(defun save-meta-read (slot)
  (handler-case
      (with-open-file (f (aref +save-names+ slot) :direction :input :external-format :utf-8)
        (with-standard-io-syntax
          (let ((data (read f nil nil)))
            (when (and data
                       (= (first  data) +save-magic+)
                       (= (second data) +save-version+))
              (let* ((g2   (third data))
                     (turn (gs-turn g2))
                     (week (1+ (floor (1- turn) 14)))
                     (men  (reduce #'+ (gs-squads g2) :key #'sq-men :end (gs-squad-count g2)))
                     (mns  #("Jan" "Feb" "Mar" "Apr" "May" "Jun" "Jul" "Aug" "Sep" "Oct" "Nov" "Dec")))
                (multiple-value-bind (day mo) (turn->date turn)
                  (list :used t :label (format nil "Wk ~D  |  ~2D ~A 1917  |  ~D men  |  ~A"
                                               week day (aref mns mo) men
                                               (dd-name (aref +diff-defs+ (gs-difficulty g2)))))))))))
    (error () nil)))

;;; ── Helpers ──────────────────────────────────────────────────────
(defun dat-str ()
  (let ((mns #("Jan" "Feb" "Mar" "Apr" "May" "Jun" "Jul" "Aug" "Sep" "Oct" "Nov" "Dec")))
    (multiple-value-bind (day mo) (turn->date (gs-turn *g*))
      (format nil "~2D ~A 1917" day (aref mns mo)))))

(defun curr-week () (1+ (floor (1- (gs-turn *g*)) 14)))

(defun add-msg (msg)
  (let ((msgs (gs-msgs *g*)))
    (loop for i from (1- +max-msgs+) downto 1 do (setf (aref msgs i) (aref msgs (1- i))))
    (setf (aref msgs 0) (subseq msg 0 (min (length msg) (1- +msg-len+))))
    (when (< (gs-msg-count *g*) +max-msgs+) (incf (gs-msg-count *g*)))))

(defun diary-add (text)
  (when (>= (gs-diary-count *g*) +max-diary+)
    (loop for i from 0 below (1- +max-diary+)
          do (setf (aref (gs-diary *g*) i) (aref (gs-diary *g*) (1+ i))))
    (setf (gs-diary-count *g*) (1- +max-diary+)))
  (setf (aref (gs-diary *g*) (gs-diary-count *g*))
        (make-de-s :turn (gs-turn *g*) :half-am (if (gs-am *g*) 1 0)
                   :text (subseq text 0 (min (length text) (1- +msg-len+)))))
  (incf (gs-diary-count *g*)))

(defun log-msg (msg) (add-msg msg) (diary-add msg))

(defun overall-mor ()
  (let ((n (gs-squad-count *g*)))
    (if (zerop n) 0
        (floor (reduce #'+ (gs-squads *g*) :key #'sq-mor :end n) n))))

(defun total-men    () (reduce #'+ (gs-squads *g*) :key #'sq-men    :end (gs-squad-count *g*)))
(defun total-wounds () (reduce #'+ (gs-squads *g*) :key #'sq-wounds :end (gs-squad-count *g*)))

(defun upg-has  (u) (logbitp u (gs-upgrades *g*)))
(defun cp-max   ()  (if (upg-has +upg-signal-wire+) 4 3))
(defun food-cap ()  (if (upg-has +upg-food-cache+) 125 100))
(defun ammo-cap ()  (if (upg-has +upg-munitions+)  125 100))

(defun res-get (id)
  (ecase id (0 (gs-food *g*)) (1 (gs-ammo *g*)) (2 (gs-meds *g*)) (3 (gs-tools *g*))))
(defun res-set (id v)
  (ecase id (0 (setf (gs-food  *g*) v)) (1 (setf (gs-ammo  *g*) v))
             (2 (setf (gs-meds  *g*) v)) (3 (setf (gs-tools *g*) v))))
(defun res-cap (id) (ecase id (0 (food-cap)) (1 (ammo-cap)) (2 50) (3 50)))

(defun find-notable (sq trait)
  (loop for i below (sq-notable-count sq)
        for n = (aref (sq-notables sq) i)
        when (and (= (ns-trait n) trait) (ns-alive n)) return n))

(defun calc-score ()
  (let* ((mul (* (dd-score-mul-x10 (aref +diff-defs+ (gs-difficulty *g*))) 0.1))
         (s   (+ (* (gs-turn *g*)  5) (* (total-men) 20) (* (overall-mor) 3)
                 (* (gs-medals *g*) 50) (* (floor (gs-hq-rep *g*) 10) 30)
                 (* (popcount% (gs-upgrades *g*)) 25))))
    (when (= (gs-over *g*) +over-win+)    (incf s 500))
    (when (= (gs-over *g*) +over-mutiny+) (decf s 200))
    (max 0 (truncate (* s mul)))))

(defun score-grade (sc)
  (cond ((>= sc 1400) "S+") ((>= sc 1000) "S ") ((>= sc 700) "A ")
        ((>= sc 500)  "B ") ((>= sc 300)  "C ") ((>= sc 120) "D ") (t "F ")))

;;; ── Initialization ───────────────────────────────────────────────
(defparameter +squad-init+
  #(("Alpha"   7 8 68 25 "Sgt. Harris" 1)
    ("Bravo"   8 8 72 15 "Sgt. Moore"  0)
    ("Charlie" 5 8 44 55 "Sgt. Lewis"  3)
    ("Delta"   6 8 61 40 "Sgt. Bell"   2)))
(defparameter +notable-init+
  #(#(("Pte. Jack" "Whitmore" 0) ("Pte. Tom"  "Beecroft" 1))
    #(("Pte. Alf"  "Morley"   2) ("Pte. Bill" "Darton"   4))
    #(("Pte. Dan"  "Pollard"  3) ("Pte. Fred" "Stubbs"   5))
    #(("Pte. Sam"  "Colby"    1) ("Pte. Ned"  "Finch"    0))))
(defparameter +init-msgs+
  #("Welcome, Captain Thorne. God help us." "ORDERS: Hold the line for 6 weeks."
    "Supply convoy expected in ~3 turns."   "Intelligence: Expect shelling tonight."))

(defun new-game (diff)
  (setf *g* (make-gs :difficulty diff :turn 1 :maxt 84 :am t
                     :weather +weather-clear+ :agg 40
                     :food 80 :ammo 60 :meds 30 :tools 40
                     :ration-level +ration-full+ :ammo-policy +ammo-normal+
                     :cmd-points 2 :hq-rep 50))
  (loop for i below 4
        do (setf (aref (gs-sector-threat *g*) i) (rng-range 20 50)))
  (setf (gs-squad-count *g*) 4)
  (loop for i below 4
        for si       = (aref +squad-init+ i)
        for notables = (map 'vector
                            (lambda (nd)
                              (make-notable-s :name  (format nil "~A ~A" (first nd) (second nd))
                                             :trait (third nd) :alive t))
                            (aref +notable-init+ i))
        do (setf (aref (gs-squads *g*) i)
                 (make-squad-s :name  (first si)  :men  (second si) :maxm (third si)
                               :mor   (fourth si) :fat  (fifth si)  :task +task-standby+
                               :has-sgt t
                               :sgt (make-sgt-s :name (sixth si) :pers (seventh si) :ok t)
                               :lore-idx (+ 4 i) :notable-count 2 :notables notables)))
  (loop for msg across +init-msgs+ do (log-msg msg))
  (setf (aref (gs-evq *g*) 0) (make-sev-s :at 3 :type +ev-supply+ :food 25 :ammo 20 :meds 8 :tools 5)
        (gs-ev-count *g*) 1))

;;; ── Render Primitives ────────────────────────────────────────────
(defun vbar ()
  (with-color (+blu+ :bold) (write-string +bv+)))

(defun hline (r kind)
  (at% r 1)
  (with-color (+blu+ :bold)
    (let* ((has-mid (member kind '(:split :join :close-r)))
           (lc (case kind (:top +tl+) (:bot +bl+) (t +lm+)))
           (rc (case kind (:top +tr+) (:bot +br+) (t +rm+)))
           (mc (case kind (:split +tm+) (:join +xx+) (:close-r +bm+) (t ""))))
      (write-string lc)
      (loop for c from 2 below +tw+
            do (write-string (if (and has-mid (= c +div+)) mc +bh+)))
      (write-string rc)))
  (flush%))

(defun draw-box (r1 c1 r2 c2)
  (at% r1 c1)
  (with-color (+blu+ :bold)
    (write-string +tl+)
    (loop for i from (1+ c1) below c2 do (write-string +bh+))
    (write-string +tr+)
    (loop for r from (1+ r1) below r2
          do (at% r c1) (write-string +bv+)
             (at% r c2) (write-string +bv+))
    (at% r2 c1)
    (write-string +bl+)
    (loop for i from (1+ c1) below c2 do (write-string +bh+))
    (write-string +br+))
  (flush%))

(defun draw-hline-mid (r c1 c2)
  (at% r c1)
  (with-color (+blu+ :bold)
    (write-string +lm+)
    (loop for i from (1+ c1) below c2 do (write-string +bh+))
    (write-string +rm+)))

(defun render-resources ()
  (let ((labs #("Food:" "Ammo:" "Meds:" "Tools:"))
        (vals (vector (gs-food *g*) (gs-ammo *g*) (gs-meds *g*) (gs-tools *g*)))
        (maxs (vector (food-cap) (ammo-cap) 50 50))
        (bcs  (vector +grn+ +yel+ +cyn+ +mag+)))
    (loop for i below 4
          for v   = (aref vals i)
          for mx  = (aref maxs i)
          do (at% (+ 4 i) 1) (vbar) (at% (+ 4 i) 2)
             (with-color (+wht+ :bold) (format t " ~6A" (aref labs i)))
             (with-color ((aref bcs i)) (format t "[~A]" (make-bar v mx 10)))
             (format t " ~3D/~3D" v mx)
             (when (>= (gs-res-hist-count *g*) 2)
               (let ((cur  (aref (gs-res-hist *g*) (mod (1- (gs-res-hist-count *g*)) +res-hist-len+) i))
                     (prev (aref (gs-res-hist *g*) (mod (- (gs-res-hist-count *g*) 2) +res-hist-len+) i)))
                 (cond ((> cur (+ prev 2)) (with-color (+grn+) (write-string +sym-up+)))
                       ((< cur (- prev 2)) (with-color (+red+) (write-string +sym-dn+)))
                       (t                  (with-color (+gry+) (write-string +sym-eq+))))
                 (write-char #\Space)))
             (write-spaces (- +lw+ 26))
             (at% (+ 4 i) +div+) (vbar) (at% (+ 4 i) (1+ +div+))
             (let ((mi (- (gs-msg-count *g*) 1 i)))
               (if (and (>= mi 0) (< mi (gs-msg-count *g*)) (plusp (length (aref (gs-msgs *g*) mi))))
                   (progn (with-color (+cyn+) (write-string " > "))
                          (with-color (+wht+) (ppad (aref (gs-msgs *g*) mi) (- +rw+ 3))))
                   (write-spaces +rw+)))
             (at% (+ 4 i) +tw+) (vbar))))

(defun render-squads-map ()
  (let* ((abar  (make-bar (gs-agg *g*) 100 9))
         (aln0  (format nil " Aggrn:[~A] ~D%" abar (gs-agg *g*)))
         (ex    (if (> (gs-agg *g*) 70) (clamp (floor (gs-agg *g*) 7) 0 14) 0))
         (aln1  (format nil " ~A~A" (make-string ex :initial-element #\!)
                        (if (> (gs-agg *g*) 70) " ATTACK LIKELY" "")))
         (sec-row (format nil " A:~3D B:~3D C:~3D D:~3D"
                          (aref (gs-sector-threat *g*) 0) (aref (gs-sector-threat *g*) 1)
                          (aref (gs-sector-threat *g*) 2) (aref (gs-sector-threat *g*) 3)))
         (upln  (let ((s (copy-seq " Upg:")))
                  (loop for i below +upg-count+ when (upg-has i)
                        do (setf s (concatenate 'string s "[" (aref +upg-shorts+ i) "]")))
                  s))
         (map (vector "                                   "
                      (concatenate 'string " " (make-string 32 :initial-element #\-) " ")
                      "  [A]  [B]  [C]  [D]   Sectors  "
                      sec-row
                      (concatenate 'string "    " +tl+ (make-string 10 :initial-element #\=) +tr+ "                 ")
                      (concatenate 'string "    " +bv+ "   H.Q.   " +bv+ "  ~~No Man's~~  ")
                      (concatenate 'string "    " +bl+ (make-string 10 :initial-element #\=) +br+ "  ~~  Land   ~~  ")
                      (if (plusp (gs-upgrades *g*)) upln "                                   ")
                      aln0 aln1)))
    (loop for r from 11 to 20
          for mi = (- r 11)
          for si = (floor mi 2)
          for ss = (mod mi 2)
          for sq = (and (< si (gs-squad-count *g*)) (aref (gs-squads *g*) si))
          do (at% r 1) (vbar) (at% r 2)
             (cond
               ((null sq) (write-spaces +lw+))
               ((zerop ss)
                (let* ((selected (= si (gs-sel *g*)))
                       (mb (make-bar (sq-mor sq) 100 7)))
                  (with-color (+wht+) (write-string (if selected " > " "   ")))
                  (when selected (bold%))
                  (with-color ((if selected +yel+ +wht+)) (format t "~7A" (sq-name sq)))
                  (with-color (+gry+) (format t "~D/~D" (sq-men sq) (sq-maxm sq)))
                  (if (plusp (sq-wounds sq))
                      (with-color (+red+) (format t "+~DW" (sq-wounds sq)))
                      (write-char #\Space))
                  (write-char #\Space)
                  (with-color ((mor-color (sq-mor sq))) (format t "[~A]" mb))
                  (write-char #\Space)
                  (with-color ((td-color (aref +task-defs+ (sq-task sq))) :bold)
                    (format t "~7A" (td-name (aref +task-defs+ (sq-task sq)))))
                  (write-spaces (- +lw+ 33))))
               (t
                (if (and (gs-orders-mode *g*) (= si (gs-sel *g*)))
                    (let* ((n  +task-count+)
                           (pv (mod (+ (gs-osel *g*) (1- n)) n))
                           (nx (mod (1+ (gs-osel *g*)) n)))
                      (with-color (+yel+ :bold) (write-string "  > "))
                      (with-color (+gry+) (ppad (td-name (aref +task-defs+ (aref +order-opts+ pv))) 7))
                      (write-char #\Space)
                      (with-color (+cyn+ :bold)
                        (format t "[~7A]" (td-name (aref +task-defs+ (aref +order-opts+ (gs-osel *g*))))))
                      (write-char #\Space)
                      (with-color (+gry+) (ppad (td-name (aref +task-defs+ (aref +order-opts+ nx))) 7))
                      (write-spaces (- +lw+ 30)))
                    (let* ((sl (if (sq-has-sgt sq)
                                   (format nil "   ~A (~A)" (ss-name (sq-sgt sq))
                                           (pd-name (aref +pers-defs+ (ss-pers (sq-sgt sq)))))
                                   "   No Sergeant"))
                           (fc (if (< (sq-fat sq) 40) +grn+ (if (< (sq-fat sq) 70) +yel+ +red+)))
                           (na (count-if #'ns-alive (sq-notables sq) :end (sq-notable-count sq))))
                      (with-color (+gry+ :dim) (ppad sl 30))
                      (write-string "Fat:")
                      (with-color (fc) (format t "~2D%" (sq-fat sq)))
                      (with-color (+gry+) (format t " N:~D" na))
                      (write-spaces (- +lw+ 41))))))
             (at% r +div+) (vbar) (at% r (1+ +div+))
             (with-color (+gry+) (ppad (aref map mi) +rw+))
             (at% r +tw+) (vbar))
    (flush%)))

(defun render ()
  (cls%)
  (hline 1 :top)
  (at% 2 1) (vbar) (at% 2 2)
  (with-color (+yel+ :bold) (write-string " BURDEN OF COMMAND"))
  (with-color (+gry+) (format t " ~A " +vt+))
  (with-color (+wht+) (write-string (subseq (dat-str) 0 6)))
  (with-color ((if (gs-am *g*) +cyn+ +mag+)) (format t " ~A" (if (gs-am *g*) "AM" "PM")))
  (with-color (+gry+) (format t " ~A " +vt+))
  (with-color ((wd-color (aref +weather-defs+ (gs-weather *g*))))
    (write-string (wd-label (aref +weather-defs+ (gs-weather *g*)))))
  (with-color (+gry+) (format t " ~A Wk~D T~D/~D ~A CP:" +vt+ (curr-week) (gs-turn *g*) (gs-maxt *g*) +vt+))
  (loop for i below (cp-max)
        for filled = (< i (gs-cmd-points *g*))
        do (fg% (if filled +yel+ +gry+))
           (if filled (bold%) (dim%))
           (write-string (if filled +sym-bl+ +sym-ci+))
           (rst%))
  (with-color (+gry+) (format t " R:~D" (gs-hq-rep *g*)))
  (with-color ((dd-color (aref +diff-defs+ (gs-difficulty *g*))) :dim)
    (let ((dn (dd-name (aref +diff-defs+ (gs-difficulty *g*)))))
      (format t " [~A]" (subseq dn 0 (min 3 (length dn))))))
  (at% 2 +tw+) (vbar) (flush%)
  ;; Row 3-9: resources
  (hline 3 :split) (render-resources)
  ;; Row 8: morale bar
  (let ((m (overall-mor)))
    (at% 8 1) (vbar) (at% 8 2)
    (with-color (+wht+ :bold) (write-string " Moral"))
    (with-color ((mor-color m)) (format t "[~A]~3D%" (make-bar m 100 8) m))
    (write-char #\Space)
    (with-color ((rd-color (aref +ration-defs+ (gs-ration-level *g*))))
      (format t "R:~A" (rd-name (aref +ration-defs+ (gs-ration-level *g*)))))
    (write-char #\Space)
    (with-color ((ad-color (aref +ammo-defs+ (gs-ammo-policy *g*))))
      (format t "A:~A" (ad-name (aref +ammo-defs+ (gs-ammo-policy *g*)))))
    (let ((tw (total-wounds)))
      (when (plusp tw) (write-char #\Space)
        (with-color (+red+ :bold) (format t "~A~DW" +sym-wn+ tw))))
    (write-spaces (- +lw+ 39))
    (at% 8 +div+) (vbar) (at% 8 (1+ +div+))
    (let ((mi4 (if (> (gs-msg-count *g*) 4) (- (gs-msg-count *g*) 5) -1)))
      (if (and (>= mi4 0) (plusp (length (aref (gs-msgs *g*) mi4))))
          (progn (with-color (+cyn+) (write-string " > "))
                 (with-color (+wht+) (ppad (aref (gs-msgs *g*) mi4) (- +rw+ 3))))
          (write-spaces +rw+)))
    (at% 8 +tw+) (vbar) (flush%))
  (hline 9 :join)
  (at% 10 2) (with-color (+wht+ :bold) (ppad " SQUADS" +lw+))
  (at% 10 (1+ +div+)) (with-color (+wht+ :bold) (ppad " SECTOR MAP" +rw+))
  (at% 10 1) (vbar) (at% 10 +div+) (vbar) (at% 10 +tw+) (vbar) (flush%)
  (render-squads-map)
  (hline 21 :close-r)
  (at% 22 1) (vbar) (at% 22 2)
  (with-color (+yel+)
    (ppad (if (gs-orders-mode *g*)
              (format nil " [<>] Cycle  [ENTER] Confirm  [ESC] Cancel")
              " [SPC] EndTurn [O] Orders [C] Cmd [R] Res [I] Intel [D] Dossier [ESC] Menu")
          (- +tw+ 2)))
  (at% 22 +tw+) (vbar) (flush%)
  (hline 23 :bot) (at% 24 1) (flush%))

;;; ── Screen: Resources ────────────────────────────────────────────
(defun res-render-overview ()
  (at% 4 4) (with-color (+wht+ :bold) (write-string " CURRENT STOCK"))
  (at% 5 4) (with-color (+gry+) (write-string (make-string 74 :initial-element #\-)))
  (loop for i below +res-count+
        for v   = (res-get i)
        for mx  = (res-cap i)
        for pct = (if (zerop mx) 0.0 (/ v (float mx)))
        do (at% (+ 6 i) 4)
           (with-color ((aref +res-colors+ i) :bold) (format t " ~7A" (aref +res-names+ i)))
           (with-color ((aref +res-colors+ i)) (format t "[~A]" (make-bar v mx 20)))
           (format t " ~3D/~3D" v mx)
           (at% (+ 6 i) 42)
           (cond ((< pct 0.15) (with-color (+red+ :bold) (write-string " CRITICAL")))
                 ((< pct 0.30) (with-color (+red+)       (write-string " LOW")))
                 ((< pct 0.60) (with-color (+yel+)       (write-string " FAIR")))
                 (t            (with-color (+grn+)        (write-string " GOOD")))))
  (at% 11 4) (with-color (+wht+ :bold) (write-string " TREND  (last 10 turns)"))
  (at% 12 4) (with-color (+gry+) (write-string (make-string 74 :initial-element #\-)))
  (loop for ri below +res-count+
        for mx = (res-cap ri)
        do (at% (+ 13 ri) 4)
           (with-color ((aref +res-colors+ ri)) (format t " ~7A" (aref +res-names+ ri)))
           (let ((n (min (gs-res-hist-count *g*) +res-hist-len+)))
             (loop for j below +res-hist-len+
                   for hi = (+ (- (gs-res-hist-count *g*) +res-hist-len+) j)
                   do (if (or (< hi 0) (>= j n))
                          (write-char #\Space)
                          (write-char
                           (char "._-=+*#@"
                                 (clamp (truncate
                                         (* (/ (aref (gs-res-hist *g*) (mod hi +res-hist-len+) ri)
                                              mx)
                                            7))
                                        0 7)))))
             (when (>= n 2)
               (let ((cur  (aref (gs-res-hist *g*) (mod (1- (gs-res-hist-count *g*)) +res-hist-len+) ri))
                     (prev (aref (gs-res-hist *g*) (mod (- (gs-res-hist-count *g*) 2) +res-hist-len+) ri)))
                 (cond ((> cur (+ prev 2)) (with-color (+grn+) (format t " ~A" +sym-up+)))
                       ((< cur (- prev 2)) (with-color (+red+) (format t " ~A" +sym-dn+)))
                       (t                  (with-color (+gry+) (format t " ~A" +sym-eq+))))))))
  (at% 18 4) (with-color (+wht+ :bold) (write-string " WOUNDED PERSONNEL"))
  (at% 19 4) (with-color (+gry+) (write-string (make-string 74 :initial-element #\-)))
  (at% 20 4)
  (let ((any 0))
    (do-squads (sq)
      (when (plusp (sq-wounds sq))
        (with-color (+red+) (format t " ~A:~DW" (sq-name sq) (sq-wounds sq)))
        (incf any)))
    (when (zerop any) (with-color (+grn+) (write-string " No walking wounded."))))
  (at% 21 4) (with-color (+wht+ :bold) (write-string " SECTOR THREATS"))
  (at% 22 4)
  (loop for i below 4
        for t% = (aref (gs-sector-threat *g*) i)
        do (with-color ((if (>= t% 70) +red+ (if (>= t% 40) +yel+ +grn+)))
             (format t "  [~A] ~3D%" (aref #("A" "B" "C" "D") i) t%))))

(defun res-render-barter (bsel)
  (at% 4 4) (with-color (+wht+ :bold) (write-string " RESOURCE EXCHANGE  (barter with field traders)"))
  (at% 5 4) (with-color (+gry+) (write-string (make-string 74 :initial-element #\-)))
  (loop for i below (length +barter-rates+)
        for b   = (aref +barter-rates+ i)
        for can = (>= (res-get (br-from b)) (br-give b))
        for row = (+ 6 i)
        when (<= row 21)
        do (at% row 4)
           (cond ((and (= i bsel) can) (with-color (+yel+ :bold) (write-string "> ")))
                 ((= i bsel)           (with-color (+gry+)       (write-string "> ")))
                 (t                    (write-string "  ")))
           (if can (with-color ((aref +res-colors+ (br-from b))) (format t "~6A" (aref +res-names+ (br-from b))))
                   (with-color (+gry+ :dim) (format t "~6A" (aref +res-names+ (br-from b)))))
           (with-color (+gry+) (format t " -~2D " (br-give b)))
           (write-string +sym-rt+)
           (with-color ((aref +res-colors+ (br-to b))) (format t " ~6A" (aref +res-names+ (br-to b))))
           (with-color (+gry+) (format t " +~2D  " (br-get b)))
           (when (= i bsel) (with-color (+wht+ :dim) (ppad (br-desc b) 40)))))

(defun res-render-policies ()
  (at% 4 4) (with-color (+wht+ :bold) (write-string " RATION POLICY"))
  (at% 5 4) (with-color (+gry+) (write-string (make-string 74 :initial-element #\-)))
  (loop for i below +ration-count+
        for r   = (aref +ration-defs+ i)
        for sel = (= (gs-ration-level *g*) i)
        do (at% (+ 6 i) 4)
           (if sel (with-color ((rd-color r) :bold) (write-string "> "))
                   (with-color (+gry+) (write-string "  ")))
           (with-color ((rd-color r)) (format t "~12A" (rd-name r)))
           (with-color ((if sel +wht+ +gry+))
             (format t " Food*~4,2F  MorDelta: ~+D/turn  " (rd-food-mul r) (rd-mor-per-turn r)))
           (with-color (+gry+ :dim) (write-string (rd-desc r))))
  (at% 11 4) (with-color (+wht+ :bold) (write-string " AMMO POLICY"))
  (at% 12 4) (with-color (+gry+) (write-string (make-string 74 :initial-element #\-)))
  (loop for i below +ammo-count+
        for a   = (aref +ammo-defs+ i)
        for sel = (= (gs-ammo-policy *g*) i)
        do (at% (+ 13 i) 4)
           (if sel (with-color ((ad-color a) :bold) (write-string "> "))
                   (with-color (+gry+) (write-string "  ")))
           (with-color ((ad-color a)) (format t "~10A" (ad-name a)))
           (with-color ((if sel +wht+ +gry+))
             (format t " Ammo*~4,2F  Patrol*~4,2F  Raid~+3,0F%%  "
                     (ad-ammo-mul a) (ad-patrol-mor-mul a) (* (ad-raid-resist-add a) 100)))
           (with-color (+gry+ :dim) (ppad (ad-desc a) 28)))
  (at% 17 4) (with-color (+wht+ :bold) (write-string " WOUND TREATMENT PROTOCOL"))
  (at% 18 4) (with-color (+gry+) (write-string (make-string 74 :initial-element #\-)))
  (at% 19 4) (with-color (+wht+) (write-string " Walking wounded consume 1 med/turn passively."))
  (at% 20 4) (with-color (+wht+) (write-string " Untreated wounds after 5 turns become fatal (-1 man)."))
  (at% 21 4)
  (if (upg-has +upg-field-hosp+)
      (with-color (+grn+) (write-string " Field Hospital: heals 1 extra wound/turn automatically."))
      (with-color (+gry+) (write-string " Field Hospital not built (use Trench Upgrades to build)."))))

(defun screen-resources ()
  (let ((tab 0) (bsel 0))
    (loop
      (cls%) (draw-box 1 2 23 79)
      (at% 1 4) (with-color (+yel+ :bold)
        (format t " RESOURCE MANAGEMENT  ~A  ~A  ~A  HQ Rep: ~D" +vt+ (dat-str) +vt+ (gs-hq-rep *g*)))
      (at% 2 4)
      (dolist (pair '((0 " [1] Overview ") (1 " [2] Barter ") (2 " [3] Policies ")))
        (if (= tab (first pair))
            (with-color (+yel+ :bold) (write-string (second pair)))
            (with-color (+gry+) (write-string (second pair)))))
      (draw-hline-mid 3 2 79)
      (case tab
        (0 (res-render-overview))
        (1 (res-render-barter bsel))
        (t (res-render-policies)))
      (draw-hline-mid 22 2 79)
      (at% 22 5)
      (with-color (+yel+)
        (case tab
          (1 (format t "[^v] Select  [ENTER] Execute  [1/2/3] Tabs  [ESC] Back"))
          (2 (format t "[^v] Change ration/ammo policy  [1/2/3] Tabs  [ESC] Back"))
          (t (write-string "[1/2/3] Tabs  [ESC] Back"))))
      (flush%)
      (let ((k (read-key)))
        (when (exit-key-p k) (return))
        (case k (:|1| (setf tab 0)) (:|2| (setf tab 1)) (:|3| (setf tab 2)))
        (case tab
          (1 (case k
               (:up    (setf bsel (mod (+ bsel (1- (length +barter-rates+))) (length +barter-rates+))))
               (:down  (setf bsel (mod (1+ bsel) (length +barter-rates+))))
               (:enter
                (let* ((b  (aref +barter-rates+ bsel))
                       (fv (res-get (br-from b))))
                  (when (>= fv (br-give b))
                    (res-set (br-from b) (- fv (br-give b)))
                    (res-set (br-to b) (clamp (+ (res-get (br-to b)) (br-get b)) 0 (res-cap (br-to b))))
                    (log-msg (format nil "Barter: -~D ~A +~D ~A."
                                     (br-give b) (aref +res-names+ (br-from b))
                                     (br-get b)  (aref +res-names+ (br-to b)))))))))
          (2 (case k
               (:up    (setf-clamp (gs-ration-level *g*) (1+ (gs-ration-level *g*)) 0 (1- +ration-count+)))
               (:down  (setf-clamp (gs-ration-level *g*) (1- (gs-ration-level *g*)) 0 (1- +ration-count+)))
               (:left  (setf-clamp (gs-ammo-policy  *g*) (1- (gs-ammo-policy  *g*)) 0 (1- +ammo-count+)))
               (:right (setf-clamp (gs-ammo-policy  *g*) (1+ (gs-ammo-policy  *g*)) 0 (1- +ammo-count+))))))))))

;;; ── Screen: Command ──────────────────────────────────────────────
(defun screen-command ()
  (let ((sel 0))
    (loop
      (cls%) (draw-box 2 5 22 75)
      (at% 2 7) (with-color (+yel+ :bold)
        (format t " COMMAND ACTIONS  ~A  Capt. Thorne  ~A  CP: ~D/~D  ~A  Rep: ~D"
                +vt+ +vt+ (gs-cmd-points *g*) (cp-max) +vt+ (gs-hq-rep *g*)))
      (draw-hline-mid 3 5 75)
      (loop for i below +cmd-count+
            for c        = (aref +cmd-defs+ i)
            for rum-free = (and (= i +cmd-rum+) (upg-has +upg-rum-store+))
            for mc       = (if (and (= i +cmd-medical+) (upg-has +upg-field-hosp+))
                               (floor (+ (cd-meds-cost c) 1) 2) (cd-meds-cost c))
            for sq       = (and (< (gs-sel *g*) (gs-squad-count *g*)) (aref (gs-squads *g*) (gs-sel *g*)))
            for can      = (and (>= (gs-cmd-points *g*) (if rum-free 0 (cd-cp-cost c)))
                                (>= (gs-food *g*) (cd-food-cost c)) (>= (gs-meds *g*) mc)
                                (not (and (= i +cmd-treat-wounded+) (or (null sq) (zerop (sq-wounds sq))))))
            for row = (+ 4 (* i 2))
            for s   = (= i sel)
            do (at% row 8)
               (cond ((not can) (with-color (+gry+ :dim)  (format t "   ~22A" (cd-name c))))
                     (s         (with-color (+yel+ :bold) (format t ">  ~22A" (cd-name c))))
                     (t         (with-color (+wht+)       (format t "   ~22A" (cd-name c)))))
               (when (and (plusp (cd-cp-cost c)) (not rum-free))
                 (with-color (+yel+) (format t "[~DCP]" (cd-cp-cost c))))
               (when (plusp (cd-food-cost c)) (with-color (+grn+) (format t "[-~DF]" (cd-food-cost c))))
               (when (plusp mc)               (with-color (+cyn+) (format t "[-~DM]" mc)))
               (when s (at% (1+ row) 10) (with-color (+gry+ :dim) (write-string (cd-effect c)))))
      (draw-hline-mid (+ 4 (* +cmd-count+ 2) -1) 5 75)
      (let ((sq (and (< (gs-sel *g*) (gs-squad-count *g*)) (aref (gs-squads *g*) (gs-sel *g*)))))
        (at% (+ 4 (* +cmd-count+ 2)) 8)
        (with-color (+yel+)
          (format t "[^v] Select  [ENTER] Execute on ~A  [ESC] Back"
                  (if sq (sq-name sq) "???")))
        (flush%)
        (let ((k (read-key)))
          (when (exit-key-p k) (return))
          (case k
            (:up    (setf sel (mod (+ sel (1- +cmd-count+)) +cmd-count+)))
            (:down  (setf sel (mod (1+ sel) +cmd-count+)))
            (:enter
             (when sq
               (let* ((c        (aref +cmd-defs+ sel))
                      (rum-free (and (= sel +cmd-rum+) (upg-has +upg-rum-store+)))
                      (mc       (if (and (= sel +cmd-medical+) (upg-has +upg-field-hosp+))
                                    (floor (+ (cd-meds-cost c) 1) 2) (cd-meds-cost c)))
                      (can      (and (>= (gs-cmd-points *g*) (if rum-free 0 (cd-cp-cost c)))
                                     (>= (gs-food *g*) (cd-food-cost c)) (>= (gs-meds *g*) mc)
                                     (not (and (= sel +cmd-treat-wounded+) (zerop (sq-wounds sq)))))))
                 (when can
                   (unless rum-free
                     (setf-clamp (gs-cmd-points *g*) (- (gs-cmd-points *g*) (cd-cp-cost c)) 0 (cp-max)))
                   (decf (gs-food *g*) (cd-food-cost c))
                   (decf (gs-meds *g*) mc)
                   (log-msg
                     (case sel
                       (#.+cmd-rum+
                        (clampf (sq-mor sq) 15)
                        (format nil "Rum ration: ~A Sq +15 morale." (sq-name sq)))
                       (#.+cmd-letters+
                        (clampf (sq-mor sq) 10)
                        (format nil "Letters home: ~A Sq +10 morale." (sq-name sq)))
                       (#.+cmd-medical+
                        (clampf (sq-fat sq) -25)
                        (format nil "Triage: ~A Sq fatigue reduced." (sq-name sq)))
                       (#.+cmd-reprimand+
                        (clampf (sq-fat sq) -8) (clampf (sq-mor sq) -5)
                        (format nil "Inspection: ~A Sq disciplined." (sq-name sq)))
                       (#.+cmd-speech+
                        (adjust-all-squads sq-mor 8)
                        "Officer's speech: company morale rises.")
                       (#.+cmd-rations-extra+
                        (adjust-all-squads sq-fat -15)
                        "Emergency rations: all fatigue reduced.")
                       (#.+cmd-ceremony+
                        (clampf (sq-mor sq) 20) (incf (gs-medals *g*))
                        (format nil "Medal ceremony: ~A Sq. +1 commendation." (sq-name sq)))
                       (#.+cmd-leave+
                        (when (> (sq-men sq) 1) (decf (sq-men sq)) (incf (sq-men-lost sq)))
                        (clampf (sq-mor sq) 15)
                        (format nil "Comp. leave: ~A Sq -1 man +15 morale." (sq-name sq)))
                       (#.+cmd-treat-wounded+
                        (let ((h (clamp (sq-wounds sq) 0 2)))
                          (setf-clamp (sq-wounds sq) (- (sq-wounds sq) h) 0 (sq-men sq))
                          (format nil "Treatment: ~D wound(s) in ~A Sq." h (sq-name sq))))
                       (#.+cmd-supply-req+
                        (if (not (gs-supply-req-pending *g*))
                            (let* ((eta (+ (gs-turn *g*) (rng-range 5 10)))
                                   (q   (if (>= (gs-hq-rep *g*) 70) 1 0))
                                   (ev  (make-sev-s :at eta :type +ev-supply+
                                          :food  (rng-range (if (= q 1) 20 10) (if (= q 1) 35 20))
                                          :ammo  (rng-range (if (= q 1) 15  8) (if (= q 1) 25 15))
                                          :meds  (rng-range (if (= q 1)  6  3) (if (= q 1) 12  8))
                                          :tools (rng-range (if (= q 1)  4  2) (if (= q 1)  8  5)))))
                              (when (< (gs-ev-count *g*) +max-evq+)
                                (setf (aref (gs-evq *g*) (gs-ev-count *g*)) ev)
                                (incf (gs-ev-count *g*)))
                              (setf (gs-supply-req-pending *g*) eta)
                              (format nil "Supply request filed. ETA ~D turns." (- eta (gs-turn *g*))))
                            "Supply request already pending."))
                       (t "Action complete.")))))))))))))

;;; ── Screen: Upgrades ─────────────────────────────────────────────
(defun screen-upgrades ()
  (let ((sel 0))
    (loop
      (cls%) (draw-box 2 4 22 77)
      (at% 2 6) (with-color (+mag+ :bold)
        (format t " TRENCH ENGINEERING  ~A  Tools: ~D" +vt+ (gs-tools *g*)))
      (draw-hline-mid 3 4 77)
      (loop for i below +upg-count+
            for u     = (aref +upg-defs+ i)
            for owned = (upg-has i)
            for can   = (and (not owned) (>= (gs-tools *g*) (ud-tools-cost u)))
            for row   = (+ 4 (* i 2))
            when (<= row 20)
            do (at% row 7)
               (cond (owned               (with-color (+grn+ :bold) (format t "  [~A] ~22A" +sym-ck+ (ud-name u))))
                     ((and (= i sel) can) (with-color (+yel+ :bold) (format t "> [~2D] ~22A" (ud-tools-cost u) (ud-name u))))
                     ((= i sel)           (with-color (+red+)       (format t "> [~2D] ~22A" (ud-tools-cost u) (ud-name u))))
                     (t                   (with-color (+gry+ :dim)  (format t "  [~2D] ~22A" (ud-tools-cost u) (ud-name u)))))
               (when (or (= i sel) owned)
                 (with-color ((if (= i sel) +wht+ +grn+) :dim) (write-string (ud-passive u)))))
      (draw-hline-mid 20 4 77)
      (at% 21 7) (with-color (+yel+) (format t "[^v] Select  [ENTER] Build  [ESC] Back"))
      (flush%)
      (let ((k (read-key)))
        (when (exit-key-p k) (return))
        (case k
          (:up    (setf sel (mod (+ sel (1- +upg-count+)) +upg-count+)))
          (:down  (setf sel (mod (1+ sel) +upg-count+)))
          (:enter
           (let ((u (aref +upg-defs+ sel)))
             (when (and (not (upg-has sel)) (>= (gs-tools *g*) (ud-tools-cost u)))
               (decf (gs-tools *g*) (ud-tools-cost u))
               (setf (gs-upgrades *g*) (logior (gs-upgrades *g*) (ash 1 sel)))
               (log-msg (format nil "Built: ~A. ~A" (ud-name u) (ud-passive u)))))))))))

;;; ── Screen: HQ Dispatch ──────────────────────────────────────────
(defun screen-hq-dispatch (idx)
  (let ((d (aref +hq-dispatches+ idx)) (sel 0))
    (loop
      (cls%) (draw-box 2 4 22 77)
      (at% 2 6) (with-color (+red+ :bold) (format t " ~A  ~A  ~A" +sym-wn+ (hd-title d) +sym-wn+))
      (draw-hline-mid 3 4 77)
      (let ((r 5))
        (dolist (line (hd-body d))
          (when (<= r 13)
            (at% r 7) (with-color (+wht+) (write-string line)) (incf r))))
      (draw-hline-mid 14 4 77)
      (at% 15 7)
      (if (= sel 0)
          (with-color (+grn+ :bold) (format t "  > ~A" (hd-comply-label d)))
          (with-color (+gry+) (format t "    ~A" (hd-comply-label d))))
      (at% 16 9) (with-color (+gry+ :dim) (write-string (hd-comply-result d)))
      (at% 17 7)
      (if (= sel 1)
          (with-color (+red+ :bold) (format t "  > ~A" (hd-defy-label d)))
          (with-color (+gry+) (format t "    ~A" (hd-defy-label d))))
      (at% 18 9) (with-color (+gry+ :dim) (write-string (hd-defy-result d)))
      (at% 19 7) (with-color (+yel+)
        (format t "Rep effect: Comply ~+D  /  Defy ~+D" (hd-cy-rep-delta d) (hd-df-rep-delta d)))
      (draw-hline-mid 20 4 77)
      (at% 21 7) (with-color (+yel+) (format t "[^v] Choose  [ENTER] Execute")) (flush%)
      (let ((k (read-key)))
        (when (or (eq k :up) (eq k :down)) (setf sel (- 1 sel)))
        (when (eq k :enter) (return (- 1 sel)))))))

;;; ── Screen: Intel ────────────────────────────────────────────────
(defun screen-intel ()
  (cls%) (draw-box 1 2 24 79)
  (at% 1 4) (with-color (+cyn+ :bold)
    (format t " INTEL REPORT  ~A  ~A  ~A  T:~D  ~A  Rep:~D"
            +vt+ (dat-str) +vt+ (gs-turn *g*) +vt+ (gs-hq-rep *g*)))
  (loop for r from 2 to 23 do (at% r 40) (with-color (+blu+ :bold) (write-string +bv+)))
  (draw-hline-mid 2 2 79)
  ;; Left column
  (at% 3 3) (with-color (+wht+ :bold) (write-string " QUEUE & DISPATCHES"))
  (at% 4 3) (with-color (+gry+) (write-string (make-string 37 :initial-element #\-)))
  (let ((lr 5))
    (loop for i below (gs-ev-count *g*) while (<= lr 10)
          for e   = (aref (gs-evq *g*) i)
          for eta = (- (ev-at e) (gs-turn *g*))
          do (at% lr 4)
             (case (ev-type e)
               (#.+ev-supply+    (with-color (+grn+) (format t " Supply: T~3D  (in ~D turn~:P)" (ev-at e) eta)))
               (#.+ev-reinforce+ (with-color (+cyn+) (format t " Reinforce: T~3D  (in ~D turn~:P)" (ev-at e) eta))))
             (incf lr))
    (when (and (gs-supply-req-pending *g*) (plusp (gs-supply-req-pending *g*)))
      (at% lr 4) (with-color (+yel+) (format t " HQ request: T~3D" (gs-supply-req-pending *g*)))))
  (at% 11 3) (with-color (+yel+ :bold) (write-string " UPCOMING HQ ORDERS"))
  (at% 12 3) (with-color (+gry+) (write-string (make-string 37 :initial-element #\-)))
  (let ((dr 13))
    (loop for i below (length +hq-dispatches+) while (<= dr 17)
          for hd = (aref +hq-dispatches+ i)
          when (and (zerop (logand (gs-dispatch-done *g*) (ash 1 i)))
                    (>= (- (hd-turn hd) (gs-turn *g*)) 0))
          do (at% dr 4)
             (with-color (+yel+) (format t " T~3D: ~A" (hd-turn hd) (subseq (hd-title hd) 0 (min 32 (length (hd-title hd))))))
             (incf dr)))
  (at% 18 3) (with-color (+mag+ :bold) (write-string " FIELD NOTES"))
  (at% 19 3) (with-color (+gry+) (write-string (make-string 37 :initial-element #\-)))
  (let ((hr 20))
    (loop for i below (length +hist-events+) while (<= hr 22)
          for he = (aref +hist-events+ i)
          for eta = (- (he-turn he) (gs-turn *g*))
          when (and (zerop (logand (gs-hist-fired *g*) (ash 1 i))) (>= eta 0) (<= eta 10))
          do (at% hr 4)
             (with-color (+gry+ :dim) (format t " T~3D: ~A" (he-turn he) (subseq (he-text he) 0 (min 34 (length (he-text he))))))
             (incf hr)))
  ;; Right column
  (at% 3 42) (with-color (+wht+ :bold) (write-string " ENEMY & SECTOR STATUS"))
  (at% 4 42) (with-color (+gry+) (write-string (make-string 37 :initial-element #\-)))
  (at% 5 42) (with-color (+wht+) (write-string " Aggression: "))
  (with-color ((if (> (gs-agg *g*) 70) +red+ (if (> (gs-agg *g*) 40) +yel+ +grn+)))
    (format t "~3D%% [~A]" (gs-agg *g*) (make-bar (gs-agg *g*) 100 14)))
  (at% 6 42) (with-color (+wht+) (write-string " Weather  : "))
  (with-color ((wd-color (aref +weather-defs+ (gs-weather *g*))))
    (write-string (wd-label (aref +weather-defs+ (gs-weather *g*)))))
  (at% 7 42) (with-color (+gry+ :dim)
    (format t "  ~A" (wfx-note (aref +weather-fx+ (gs-weather *g*)))))
  (at% 8 42) (with-color (+wht+ :bold) (write-string " Sector threats:"))
  (loop for i below 4
        for t% = (aref (gs-sector-threat *g*) i)
        do (at% (+ 9 i) 43)
           (with-color ((if (>= t% 70) +red+ (if (>= t% 40) +yel+ +grn+)))
             (format t " [~A] ~3D%% [~A]" (aref #("A" "B" "C" "D") i) t% (make-bar t% 100 7))))
  (at% 14 42) (with-color (+wht+ :bold) (write-string " RESOURCE FORECAST"))
  (at% 15 42) (with-color (+gry+) (write-string (make-string 37 :initial-element #\-)))
  (let* ((fm (* (dd-food-mul (aref +diff-defs+ (gs-difficulty *g*)))
                (rd-food-mul (aref +ration-defs+ (gs-ration-level *g*)))))
         (fc (+ (truncate (+ (* (clamp (floor (total-men) 6) 1 99) fm) 0.5))
                (wfx-food-extra (aref +weather-fx+ (gs-weather *g*)))))
         (am (ad-ammo-mul (aref +ammo-defs+ (gs-ammo-policy *g*))))
         (ac (reduce (lambda (s sq) (+ s (truncate (+ (* (td-ammo-cost (aref +task-defs+ (sq-task sq))) am) 0.5))))
                     (gs-squads *g*) :initial-value 0 :end (gs-squad-count *g*)))
         (tg (reduce (lambda (s sq) (+ s (td-tools-gain (aref +task-defs+ (sq-task sq)))))
                     (gs-squads *g*) :initial-value 0 :end (gs-squad-count *g*)))
         (fg (reduce (lambda (s sq) (+ s (td-food-gain  (aref +task-defs+ (sq-task sq)))))
                     (gs-squads *g*) :initial-value 0 :end (gs-squad-count *g*)))
         (ag (reduce (lambda (s sq) (+ s (td-ammo-gain  (aref +task-defs+ (sq-task sq)))))
                     (gs-squads *g*) :initial-value 0 :end (gs-squad-count *g*))))
    (at% 16 43) (with-color (+grn+) (format t " Food  ~3D -> ~~~3D (~+D)" (gs-food  *g*) (clamp (+ (gs-food  *g*) fg (- fc)) 0 (food-cap)) (- fg fc)))
    (at% 17 43) (with-color (+yel+) (format t " Ammo  ~3D -> ~~~3D (~+D)" (gs-ammo  *g*) (clamp (+ (gs-ammo  *g*) ag (- ac)) 0 (ammo-cap)) (- ag ac)))
    (at% 18 43) (with-color (+cyn+) (format t " Meds  ~3D -> ~~~3D (~+D)" (gs-meds  *g*) (clamp (- (gs-meds  *g*) (total-wounds)) 0 50) (- (total-wounds))))
    (at% 19 43) (with-color (+mag+) (format t " Tools ~3D -> ~~~3D (~+D)" (gs-tools *g*) (clamp (+ (gs-tools *g*) tg) 0 50) tg)))
  (at% 21 42) (with-color (+wht+ :bold) (write-string " TRENCH STATUS")) (at% 22 42)
  (let ((uc 0))
    (loop for i below +upg-count+ when (upg-has i)
          do (with-color (+grn+) (format t " ~A" (ud-name (aref +upg-defs+ i)))) (incf uc))
    (when (zerop uc) (with-color (+gry+) (write-string " No upgrades built."))))
  (at% 24 4) (with-color (+yel+) (write-string "[ESC] Back to command")) (flush%)
  (read-key))

;;; ── Screen: Dossier ──────────────────────────────────────────────
(defun screen-dossier (start-idx)
  (when (or (< start-idx 0) (>= start-idx (gs-squad-count *g*))) (return-from screen-dossier))
  (let ((idx start-idx))
    (loop
      (let ((sq (aref (gs-squads *g*) idx)))
        (cls%) (draw-box 1 2 23 79)
        (at% 1 4) (with-color (+yel+ :bold)
          (format t " DOSSIER: ~A Section  ~A  ~A  ~A  Sgt: ~A"
                  (sq-name sq) +vt+ (pd-name (aref +pers-defs+ (ss-pers (sq-sgt sq)))) +vt+
                  (if (sq-has-sgt sq) (ss-name (sq-sgt sq)) "None")))
        (loop for r from 2 to 22 do (at% r 40) (with-color (+blu+ :bold) (write-string +bv+)))
        (draw-hline-mid 2 2 79)
        ;; Left: squad status
        (at% 3 3) (with-color (+wht+ :bold) (write-string " SQUAD STATUS"))
        (at% 4 3) (with-color (+gry+) (write-string (make-string 37 :initial-element #\-)))
        (at% 5 3) (with-color (+wht+) (format t " Strength : ~D/~D [~A]" (sq-men sq) (sq-maxm sq) (make-bar (sq-men sq) (sq-maxm sq) 12)))
        (at% 6 3) (with-color (+wht+) (format t " Morale   : ~3D%% [~A] " (sq-mor sq) (make-bar (sq-mor sq) 100 12)))
        (with-color ((mor-color (sq-mor sq))) (write-string (mor-label (sq-mor sq))))
        (at% 7 3) (with-color (+wht+) (format t " Fatigue  : ~3D%% [~A]" (sq-fat sq) (make-bar (sq-fat sq) 100 12)))
        (at% 8 3) (with-color (+wht+) (format t " Wounds   : ~D walking wounded" (sq-wounds sq)))
        (when (plusp (sq-wounds sq)) (with-color (+red+) (write-string " (!)")))
        (at% 9 3)
        (with-color (+wht+) (write-string " Task     : "))
        (with-color ((td-color (aref +task-defs+ (sq-task sq))) :bold)
          (write-string (td-name (aref +task-defs+ (sq-task sq)))))
        (with-color (+gry+ :dim) (format t "  ~A" (td-desc (aref +task-defs+ (sq-task sq)))))
        (at% 10 3) (with-color (+gry+) (write-string (make-string 37 :initial-element #\-)))
        (at% 11 3) (with-color (+wht+ :bold) (write-string " NOTABLE MEN"))
        (loop for j below (sq-notable-count sq)
              for n = (aref (sq-notables sq) j)
              do (at% (+ 12 j) 3)
                 (if (not (ns-alive n))
                     (with-color (+gry+ :dim) (format t " ~A  [KIA]" (ns-name n)))
                     (progn
                       (with-color ((trd-color (aref +trait-defs+ (ns-trait n)))) (format t " ~A" (ns-name n)))
                       (with-color (+gry+ :dim) (format t "  [~A] ~A" (trd-name (aref +trait-defs+ (ns-trait n))) (trd-effect (aref +trait-defs+ (ns-trait n))))))))
        (at% 14 3) (with-color (+gry+) (write-string (make-string 37 :initial-element #\-)))
        (at% 15 3) (with-color (+wht+ :bold) (write-string " COMBAT RECORD"))
        (at% 16 3) (with-color (+gry+) (write-string " Raids repelled: ")) (with-color (+grn+) (format t "~D" (sq-raids-repelled sq)))
        (at% 17 3) (with-color (+gry+) (write-string " Men lost      : ")) (with-color (+red+) (format t "~D" (sq-men-lost sq)))
        (at% 18 3) (with-color (+gry+) (write-string " Turns in line : ")) (with-color (+cyn+) (format t "~D" (sq-turns-alive sq)))
        (at% 19 3) (with-color (+wht+) (write-string " Sgt status: "))
        (cond ((not (sq-has-sgt sq))       (with-color (+red+) (write-string "No sergeant.")))
              ((not (ss-ok (sq-sgt sq)))   (with-color (+red+) (write-string "Shell shock -- unfit.")))
              (t (with-color (+grn+) (format t "Active -- ~A" (pd-effect-str (aref +pers-defs+ (ss-pers (sq-sgt sq))))))))
        ;; Right: personnel file (codex lore)
        (at% 3 41) (with-color (+wht+ :bold) (write-string " PERSONNEL FILE"))
        (at% 4 41) (with-color (+gry+) (write-string (make-string 38 :initial-element #\-)))
        (let ((li (sq-lore-idx sq)))
          (when (and (>= li 0) (< li (length +codex+)))
            (let ((c (aref +codex+ li)))
              (at% 5 41) (with-color ((ce-title-color c) :bold) (write-string (ce-title c)))
              (at% 6 41) (with-color (+gry+) (write-string (make-string 38 :initial-element #\-)))
              (let ((rr 7))
                (dolist (line (ce-lines c))
                  (when (<= rr 21)
                    (at% rr 41) (with-color (+wht+ :dim) (ppad line 37))
                    (incf rr)))))))
        (draw-hline-mid 22 2 79)
        (at% 22 5) (with-color (+yel+) (format t "[1-4] Switch  [<>] Prev/Next  [ESC] Back"))
        (flush%)
        (let ((k (read-key)))
          (when (exit-key-p k) (return))
          (case k
            (:left  (when (> idx 0) (decf idx)))
            (:right (when (< idx (1- (gs-squad-count *g*))) (incf idx)))
            (:|1| (when (>= (gs-squad-count *g*) 1) (setf idx 0)))
            (:|2| (when (>= (gs-squad-count *g*) 2) (setf idx 1)))
            (:|3| (when (>= (gs-squad-count *g*) 3) (setf idx 2)))
            (:|4| (when (>= (gs-squad-count *g*) 4) (setf idx 3)))))))))

;;; ── Screen: How to Play ──────────────────────────────────────────
(defstruct (hp (:conc-name hp-)) title title-color lines)
(defparameter +htp-pages+
  (vector
   (make-hp :title "THE BASICS -- OBJECTIVE & TURNS" :title-color +yel+
    :lines '("You are Captain Alistair Thorne, 11th East Lancashire Regiment, 1917." "Your company holds a trench sector near Passchendaele, Flanders." "Brigade HQ has ordered you to hold the line for six weeks. Survive." "~" "~ TURNS" "The game runs for 84 turns -- 6 weeks x 7 days x 2 half-days (AM/PM)." "Press SPACE to end your turn. Events and decay fire between turns." "One command point regenerates each AM turn automatically." "~" "~ END STATES" "| VICTORY  | Survive all 84 turns -- your unit is relieved from the line. |" "| DEFEAT   | Every man in every squad has been killed.                   |" "| MUTINY   | All squads simultaneously fall below 5 morale.             |" "~" "There is no 'winning' in the usual sense. You endure. That is enough."))
   (make-hp :title "SQUADS & SERGEANTS" :title-color +cyn+
    :lines '("You command four sections, each led by a sergeant with a personality." "Select a squad with the arrow keys. Press D to view the full Dossier." "~" "| ALPHA   | Sgt. Harris | Brave     | 7/8 men | Good morale  |" "| BRAVO   | Sgt. Moore  | Steadfast | 8/8 men | Good morale  |" "| CHARLIE | Sgt. Lewis  | Drunkard  | 5/8 men | Poor morale  |" "| DELTA   | Sgt. Bell   | Cowardly  | 6/8 men | Fair morale  |" "~" "~ PERSONALITY  multiplies all fatigue and morale changes" "| Steadfast | x1.00 | Reliable. No penalty or bonus.             |" "| Brave     | x1.20 | Pushes hard. Burns fast. Worth keeping busy.|" "| Cowardly  | x0.70 | Low effectiveness. Poor under sustained fire.|" "| Drunkard  | x0.85 | Slightly below standard. Unpredictable.    |" "~" "A squad at 0 morale may mutiny. A squad at 0 men is gone for good."))
   (make-hp :title "SQUAD ORDERS" :title-color +mag+
    :lines '("Press O then LEFT/RIGHT to cycle orders, ENTER to confirm." "~" "| STANDBY  | -5fat  0mor  | Low ammo  | Safe. Rest in place.            |" "| PATROL   | +10fat +3mor | Med ammo  | Scout sector. Best morale gain. |" "| RAID     | +20fat +4mor | High ammo | Strike enemy. High risk/reward. |" "| REPAIR   | +5fat  0mor  | Min ammo  | Generate tools. Fix the trench. |" "| REST     | -15fat +5mor | Min ammo  | Fastest fatigue and morale cure.|" "| FORAGE   | +12fat -2mor | No ammo   | Find food. Costs morale.        |" "| SCAVENGE | +15fat -3mor | No ammo   | Find ammo+tools. Low defence.   |" "~" "~ KEY RULES" "Squads above 80% fatigue lose 4 extra morale per turn." "PATROL bonus is multiplied by your current Ammo Policy (see page 5)." "Below 10% morale a squad may desert one man per turn." "FORAGE and SCAVENGE leave squads exposed -- watch sector threats."))
   (make-hp :title "RESOURCES -- FOOD, AMMO, MEDS, TOOLS" :title-color +grn+
    :lines '("Four resources must be balanced across 84 turns." "~" "| FOOD  | Cap 100 (125*) | Consumed by men alive each turn.        |" "| AMMO  | Cap 100 (125*) | Spent by squads per their task.         |" "| MEDS  | Cap  50        | Spent treating wounds and gas attacks.  |" "| TOOLS | Cap  50        | Spent on upgrades; earned by REPAIR.    |" "  * Cap raised by Food Cache / Munitions Store upgrades." "~" "~ CRITICAL THRESHOLDS" "| Food < 15 | All squads lose 5 morale per turn immediately.     |" "| Ammo < 10 | PATROL morale bonus is cancelled entirely.         |" "| Meds = 0  | Each wound has a 15% chance per turn of killing.   |" "~" "Press R for the Resource Management screen." "There you can view trends, barter resources, set policies." "Convoys arrive automatically. You can also request one (2 CP)."))
   (make-hp :title "RATION POLICY, AMMO POLICY & BARTER" :title-color +yel+
    :lines '("Set in Resources (R) -> Policies tab with arrow keys." "~" "~ RATION POLICY  food multiplier + morale effect per squad per turn" "| FULL      | x1.00 |  0 mor | Standard. No penalty.                |" "| HALF      | x0.75 | -1 mor | Manageable short-term.               |" "| QUARTER   | x0.55 | -4 mor | Heavy damage within a week.          |" "| EMERGENCY | x0.30 | -8 mor | Last resort. Men will not endure long.|" "~" "~ AMMO POLICY  affects consumption, patrol bonus, and raid resistance" "| CONSERVE | x0.60 ammo | x0.60 patrol bonus | -10% raid resist |" "| NORMAL   | x1.00 ammo | x1.00 patrol bonus |   0% raid resist |" "| LIBERAL  | x1.50 ammo | x1.40 patrol bonus | +15% raid resist |" "~" "~ BARTER  (Resources -> Barter tab)" "Trade any resource for another at unfavourable frontline rates." "Example: 20 food -> 8 ammo. Use only when a resource turns critical."))
   (make-hp :title "COMMAND POINTS & COMMAND ACTIONS" :title-color +cyn+
    :lines '("CP represent your personal leadership reserve (shown as dots in header)." "Max 3 per day (4 with Signal Wire upgrade). 1 regenerates each AM turn." "Press C to open Command Actions and spend them." "~" "| Rum Ration          | 1CP -5food | +15 morale to selected squad       |" "| Write Letters       | 1CP        | +10 morale to selected squad       |" "| Medical Triage      | 1CP -5meds | -25 fatigue on selected squad      |" "| Inspect/Reprimand   | FREE       | -8 fatigue, -5 morale  (no CP)     |" "| Officer's Speech    | 2CP        | +8 morale on ALL squads            |" "| Emergency Rations   | 2CP -20food| -15 fatigue on ALL squads          |" "| Medal Ceremony      | 3CP        | +20 morale selected; +1 medal      |" "| Compassionate Leave | 2CP        | -1 man but +15 morale              |" "| Treat Wounded       | 1CP -3meds | Heal 2 wounds in selected squad    |" "| Supply Request      | 2CP        | Petition HQ (quality scales w/rep) |" "~" "Rum Store upgrade makes Rum Ration cost 0 CP (food cost remains)."))
   (make-hp :title "WOUNDS & MEDICAL CARE" :title-color +red+
    :lines '("Artillery, raids, gas, snipers, and friendly fire create wounded men." "~" "Each wound drains 1 med per turn passively -- automatically." "If meds reach 0, every wound has a 15% chance per turn of killing." "~" "~ HOW TO TREAT WOUNDS" "| Treat Wounded (C menu) | 1CP -3meds  | Heals up to 2 wounds instantly  |" "| Field Hospital upgrade |             | Auto-heals 1 wound per turn     |" "| Medic notable soldier  |             | Auto-heals 1 wound per turn     |" "| Natural recovery event |             | Rare random event               |" "~" "Build the Field Hospital early if raids are frequent." "If meds are critically low and wounds are high: Treat Wounded first." "A man who dies of an untreated wound is a permanent loss."))
   (make-hp :title "TRENCH UPGRADES & NOTABLE SOLDIERS" :title-color +mag+
    :lines '("~ TRENCH UPGRADES  (Pause menu -> Trench Upgrades, spend tools)" "| Duckboards       |  8t | Rain/storm fatigue halved.                |" "| Sandbag Revetmts | 12t | Artillery casualties reduced by 1.        |" "| Officers' Dugout | 15t | +1 morale per turn for all squads.        |" "| Lewis Gun Nest   | 20t | +15% raid resistance for all squads.      |" "| Signal Wire      |  8t | +1 CP per AM turn (4 CP max).             |" "| Field Hospital   | 18t | Auto-heals 1 wound per turn.              |" "| Observation Post | 14t | Sniper/raid events reduced by 25%.        |" "| Munitions Store  | 12t | Ammo cap +25; ammo damp events stopped.   |" "| Food Cache       | 10t | Food cap +25; rat/spoilage events stopped.|" "~" "~ NOTABLE SOLDIERS  (2 per squad, visible in Dossier)" "| Sharpshooter | +10% raid resist; can intercept snipers          |" "| Cook         | Saves 1 food per turn passively                  |" "| Medic        | Auto-treats 1 wound per turn (costs 1 med)       |" "| Scrounger    | Randomly finds 1 ammo or 1 tool per turn         |" "| Musician     | +2 morale per turn passively                     |" "| Runner       | Supply convoy ETA reduced by 1 turn              |"))
   (make-hp :title "HQ REPUTATION, DISPATCHES & SECTOR THREATS" :title-color +yel+
    :lines '("~ HQ REPUTATION  (0-100, header shows Rep:N)" "High rep (>70) means bigger supply convoys and better request outcomes." "Modified by how you respond to the five scripted HQ Dispatches." "~" "~ HQ DISPATCHES  five orders arrive at fixed turns -- comply or defy" "| T5  | Night Raid    | Comply: -agg, -ammo      / Defy: +agg, -morale |" "| T16 | Ration Cut    | Comply: -food, +rep      / Defy: convoy delayed  |" "| T28 | Pioneer Loan  | Comply: -2men, +ammo     / Defy: +agg, -morale  |" "| T44 | Def. Posture  | Comply: STANDBY, +ammo   / Defy: sector gets hit|" "| T60 | VC Nomination | Comply: +morale, +medal  / Defy: -morale, -rep  |" "~" "~ SECTOR THREATS  (sectors A-D, each 0-100)" "Threat RISES with aggression and Sector Assault random events." "Threat FALLS when the corresponding squad is on PATROL or RAID." "High threat means more frequent raids and artillery in that sector."))
   (make-hp :title "DIFFICULTY, SCORING & KEY CONTROLS" :title-color +grn+
    :lines '("~ DIFFICULTY  chosen at campaign start, cannot be changed mid-game" "| Green Fields  | x0.6 events | x0.7 morale | x0.8 score | Saves: YES |" "| Into the Mud  | x1.0 events | x1.0 morale | x1.0 score | Saves: YES |" "| No Man's Land | x1.5 events | x1.3 morale | x1.4 score | Saves: YES |" "| God Help Us   | x1.8 events | x1.5 morale | x2.0 score | Saves: NO  |" "~" "~ SCORING" "Turns x5 + Men x20 + Morale x3 + Medals x50 + Rep/10 x30 + Upgrades x25" "+500 (victory) - 200 (mutiny)  x difficulty multiplier" "Grades: S+(1400+) S(1000+) A(700+) B(500+) C(300+) D(120+) F(<120)" "~" "~ KEY CONTROLS" "| SPACE | End turn    | O | Orders        | C | Command actions |" "| R     | Resources   | I | Intel report  | D | Squad dossier   |" "| ESC   | Pause menu  | Q | Quit to menu  |   |                 |" "~" "Good luck, Captain Thorne. God help us."))))

(defun screen-how-to-play ()
  (let ((page 0) (np (length +htp-pages+)))
    (loop
      (let ((p (aref +htp-pages+ page)))
        (cls%)
        (at% 1 1) (with-color (+blu+ :bold) (write-string +tl+) (loop for i from 2 below +tw+ do (write-string +bh+)) (write-string +tr+))
        (at% 1 3) (with-color ((hp-title-color p) :bold)
          (format t " HOW TO PLAY  ~A  ~40A  ~A  pg ~D/~D" +vt+ (hp-title p) +vt+ (1+ page) np))
        (at% 2 1) (with-color (+blu+ :bold) (write-string +lm+) (loop for i from 2 below +tw+ do (write-string +bh+)) (write-string +rm+))
        (loop for r from 3 to 21 do (at% r 1) (with-color (+blu+ :bold) (write-string +bv+)) (at% r +tw+) (with-color (+blu+ :bold) (write-string +bv+)))
        (let ((row 3))
          (dolist (ln (hp-lines p))
            (when (<= row 21)
              (at% row 3)
              (cond
                ((and (plusp (length ln)) (char= (char ln 0) #\~))
                 (if (and (>= (length ln) 2) (char= (char ln 1) #\Space))
                     (with-color ((hp-title-color p) :bold) (format t "  ~A" (subseq ln 2)))
                     (with-color (+gry+ :dim) (write-string (make-string (- +tw+ 4) :initial-element #\-)))))
                ((and (plusp (length ln)) (char= (char ln 0) #\|))
                 (with-color (+gry+) (write-string "  "))
                 (let ((ci 0))
                   (loop for ci2 from 1 below (length ln)
                         for ch = (char ln ci2)
                         do (if (char= ch #\|)
                                (progn (write-char #\Space) (with-color (+gry+) (write-char #\|)) (write-char #\Space) (incf ci))
                                (progn (with-color ((if (evenp ci) +wht+ +gry+)) (write-char ch)))))))
                (t (with-color (+wht+) (format t "  ~A" ln))))
              (incf row))))
        (at% 22 1) (with-color (+blu+ :bold) (write-string +lm+) (loop for i from 2 below +tw+ do (write-string +bh+)) (write-string +rm+))
        (at% 22 3)
        (loop for i below np
              do (if (= i page)
                     (with-color ((hp-title-color p) :bold) (write-string +sym-bl+))
                     (with-color (+gry+ :dim) (write-string +sym-ci+))))
        (at% 22 (- +tw+ 46)) (with-color (+yel+)
          (format t "[<] Prev  [>] Next  [1-9][0] Jump  [ESC] Back"))
        (at% 23 1) (with-color (+blu+ :bold) (write-string +bl+) (loop for i from 2 below +tw+ do (write-string +bh+)) (write-string +br+))
        (flush%)
        (let ((k (read-key)))
          (when (exit-key-p k) (return))
          (case k
            ((:left :up)    (setf page (mod (+ page (1- np)) np)))
            ((:right :down) (setf page (mod (1+ page) np))))
          (loop for digit from 1 to 9
                when (eq k (intern (write-to-string digit) :keyword))
                do (setf page (1- digit)))
          (when (eq k :|0|) (setf page 9)))))))

;;; ── Screen: Codex ────────────────────────────────────────────────
(defun screen-codex (start)
  (let ((idx (clamp start 0 (1- (length +codex+)))))
    (loop
      (let ((c (aref +codex+ idx)))
        (cls%) (draw-box 1 2 23 79)
        (at% 1 4) (with-color (+yel+ :bold)
          (format t " CODEX  ~A  ~D/~D  ~A  ~38A" +vt+ (1+ idx) (length +codex+) +vt+ (ce-title c)))
        (draw-hline-mid 2 2 79)
        (at% 4 5) (with-color ((ce-title-color c) :bold) (write-string (ce-title c)))
        (at% 5 5) (with-color (+gry+) (write-string (make-string 72 :initial-element #\-)))
        (let ((r 7))
          (dolist (line (ce-lines c))
            (when (<= r 20)
              (at% r 5) (with-color (+wht+) (write-string line)) (incf r))))
        (draw-hline-mid 21 2 79)
        (at% 22 5) (with-color (+yel+) (format t "[<] Prev  [>] Next  [1-9] Jump  [ESC] Back"))
        (flush%)
        (let ((k (read-key)))
          (when (exit-key-p k) (return))
          (case k
            ((:left :up)    (setf idx (mod (+ idx (1- (length +codex+))) (length +codex+))))
            ((:right :down) (setf idx (mod (1+ idx) (length +codex+)))))
          (loop for digit from 1 to 9
                when (eq k (intern (write-to-string digit) :keyword))
                do (let ((ji (1- digit)))
                     (when (< ji (length +codex+)) (setf idx ji)))))))))

;;; ── Screen: Diary ────────────────────────────────────────────────
(defun screen-diary ()
  (let ((scroll 0)
        (mns #("Jan" "Feb" "Mar" "Apr" "May" "Jun" "Jul" "Aug" "Sep" "Oct" "Nov" "Dec")))
    (loop
      (cls%) (draw-box 1 2 23 79)
      (at% 1 4) (with-color (+yel+ :bold)
        (format t " FIELD DIARY  ~A  ~D entries" +vt+ (gs-diary-count *g*)))
      (draw-hline-mid 2 2 79)
      (let ((vis 16) (total (gs-diary-count *g*)))
        (loop for i below vis
              for ei = (- total 1 (+ scroll i))
              do (at% (+ 3 i) 2) (with-color (+blu+ :bold) (write-string +bv+))
                 (at% (+ 3 i) 79) (with-color (+blu+ :bold) (write-string +bv+))
                 (when (and (>= ei 0) (< ei total))
                   (at% (+ 3 i) 3)
                   (let ((e (aref (gs-diary *g*) ei)))
                     (multiple-value-bind (day mo) (turn->date (de-turn e))
                       (with-color (+gry+)
                         (format t " T~3D ~A ~2D ~3A  "
                                 (de-turn e) (if (= (de-half-am e) 1) "AM" "PM")
                                 day (aref mns mo))))
                     (with-color (+wht+) (ppad (de-text e) (- +tw+ 22))))))
        (draw-hline-mid 20 2 79)
        (at% 21 4) (with-color (+gry+) (format t " ~D-~D of ~D" (- total scroll) (max 1 (- total (+ scroll vis -1))) total))
        (at% 22 4) (with-color (+yel+) (format t "[^v] Scroll  [ESC] Back"))
        (flush%)
        (let ((k (read-key)))
          (when (exit-key-p k) (return))
          (case k
            (:down (when (< (+ scroll vis) total) (incf scroll)))
            (:up   (when (plusp scroll) (decf scroll)))))))))

;;; ── Screen: Save / Load ──────────────────────────────────────────
(defun screen-save-load (is-save)
  (let ((metas (loop for i below +save-slots+ collect (save-meta-read i))) (sel 0))
    (loop
      (cls%) (draw-box 6 12 (+ 6 (* +save-slots+ 2) 3) 68)
      (at% 6 14) (with-color (+yel+ :bold)
        (write-string (if (= is-save 1) " SAVE GAME -- Choose a slot" " LOAD GAME -- Choose a slot")))
      (draw-hline-mid 7 12 68)
      (loop for i below +save-slots+
            for m = (nth i metas)
            for disabled = (and (= is-save 0) (null m))
            for text = (if m (format nil "Slot ~D  |  ~A" (1+ i) (getf m :label))
                             (format nil "Slot ~D  |  (empty)" (1+ i)))
            do (at% (+ 8 (* i 2)) 14)
               (cond (disabled    (with-color (+gry+ :dim) (format t "   ~A" text)))
                     ((= i sel)   (with-color (+yel+ :bold) (format t ">  ~A" text)))
                     (t           (with-color (+wht+) (format t "   ~A" text)))))
      (at% (+ 8 (* +save-slots+ 2)) 14)
      (with-color (+gry+) (format t "[^v] Nav  [ENTER] Select  [ESC] Cancel"))
      (flush%)
      (let ((k (read-key)))
        (when (eq k :esc) (return -1))
        (case k
          (:up    (loop do (setf sel (mod (+ sel (1- +save-slots+)) +save-slots+))
                        while (and (= is-save 0) (null (nth sel metas)))))
          (:down  (loop do (setf sel (mod (1+ sel) +save-slots+))
                        while (and (= is-save 0) (null (nth sel metas)))))
          (:enter (when (or (= is-save 1) (nth sel metas)) (return sel))))))))

;;; ── Screen: Difficulty ───────────────────────────────────────────
(defun screen-difficulty ()
  (let ((sel 1))
    (loop
      (cls%) (draw-box 3 8 16 72)
      (at% 3 10) (with-color (+yel+ :bold) (write-string " SELECT DIFFICULTY"))
      (draw-hline-mid 4 8 72)
      (loop for i below +diff-count+
            for dd  = (aref +diff-defs+ i)
            for sel? = (= i sel)
            do (at% (+ 5 (* i 2)) 11)
               (if sel?
                   (with-color ((dd-color dd) :bold) (format t "> ~14A" (dd-name dd)))
                   (with-color (+gry+) (format t "  ~14A" (dd-name dd))))
               (at% (+ 6 (* i 2)) 13)
               (with-color ((if sel? +wht+ +gry+) :dim)
                 (let ((sub (dd-subtitle dd)))
                   (write-string (subseq sub 0 (min (length sub) 57))))))
      (draw-hline-mid 14 8 72)
      (at% 15 11) (with-color (+gry+) (format t "[^v] Nav  [ENTER] Confirm  [ESC] Back"))
      (flush%)
      (let ((k (read-key)))
        (when (eq k :esc) (return +diff-normal+))
        (case k
          (:up    (setf sel (mod (+ sel (1- +diff-count+)) +diff-count+)))
          (:down  (setf sel (mod (1+ sel) +diff-count+)))
          (:enter (return sel)))))))

;;; ── Screen: Main Menu ────────────────────────────────────────────
(defun screen-main-menu ()
  (let* ((any-save (loop for i below +save-slots+ thereis (save-meta-read i)))
         (opts #("New Campaign" "Load Game" "How to Play" "Codex & Lore" "Credits" "Quit"))
         (n 6) (sel 0))
    (loop
      (cls%)
      (at% 2 18) (with-color (+yel+ :bold)
        (write-string +tl+) (loop for i below 44 do (write-string +bh+)) (write-string +tr+))
      (at% 3 18) (format t "~A  B U R D E N   O F   C O M M A N D      ~A" +bv+ +bv+)
      (at% 4 18) (format t "~A     A  W W I  T r e n c h  T y c o o n  ~A" +bv+ +bv+)
      (at% 5 18) (with-color (+yel+ :bold)
        (write-string +bl+) (loop for i below 44 do (write-string +bh+)) (write-string +br+))
      (at% 6 22) (with-color (+gry+)
        (format t "11th East Lancashire Regt.  ~A  Passchendaele  ~A  1917" +vt+ +vt+))
      (let* ((bw 40) (br (1+ (floor (- +tw+ bw) 2))))
        (draw-box 8 br (+ 8 (* n 2) 2) (+ br bw -1))
        (loop for i below n
              for disabled = (and (= i 1) (not any-save))
              for row = (+ 9 (* i 2))
              do (at% row (+ br 2))
                 (cond (disabled  (with-color (+gry+ :dim)  (format t "    ~A" (aref opts i))))
                       ((= i sel) (with-color (+yel+ :bold) (format t "  > ~A" (aref opts i))))
                       (t         (with-color (+wht+)       (format t "    ~A" (aref opts i))))))
        (draw-hline-mid (+ 8 (* n 2) 1) br (+ br bw -1))
        (at% (+ 9 (* n 2)) (+ br 4)) (with-color (+gry+) (format t "[^v] Nav  [ENTER] Select"))
        (flush%))
      (let* ((disabled? (lambda (i) (and (= i 1) (not any-save))))
             (k (read-key)))
        (when (or (eq k :Q) (eq k :esc)) (return 5))
        (case k
          (:up    (loop do (setf sel (mod (+ sel (1- n)) n)) while (funcall disabled? sel)))
          (:down  (loop do (setf sel (mod (1+ sel) n))       while (funcall disabled? sel)))
          (:enter (unless (funcall disabled? sel) (return sel))))))))

;;; ── Screen: Pause Menu ───────────────────────────────────────────
(defun screen-pause-menu ()
  (let* ((ironman (= (gs-difficulty *g*) +diff-ironman+))
         (opts #("Resume" "How to Play" "Save Game" "Load Game" "Codex & Lore"
                 "Field Diary" "Squad Dossier" "Trench Upgrades" "Quit to Main Menu"))
         (n 9) (sel 0))
    (loop
      (cls%)
      (let* ((bw 52) (br (1+ (floor (- +tw+ bw) 2))))
        (draw-box 4 br (+ 4 (* n 2) 3) (+ br bw -1))
        (at% 4 (+ br 2)) (with-color (+yel+ :bold)
          (format t " PAUSED  ~A  11th East Lancashire Regt." +vt+))
        (draw-hline-mid 5 br (+ br bw -1))
        (loop for i below n
              for disabled = (and (= i 3) ironman)
              for row = (+ 6 (* i 2))
              do (at% row (+ br 2))
                 (cond (disabled  (with-color (+gry+ :dim)  (format t "    ~A" (aref opts i))))
                       ((= i sel) (with-color (+yel+ :bold) (format t "  > ~A" (aref opts i))))
                       (t         (with-color (+wht+)       (format t "    ~A" (aref opts i))))))
        (at% (+ 6 (* n 2)) (+ br 2))
        (with-color (+gry+) (format t "[^v] Nav  [ENTER] Select  [ESC] Resume"))
        (flush%)
        (let ((k (read-key)))
          (when (eq k :esc) (return 0))
          (case k
            (:up    (loop do (setf sel (mod (+ sel (1- n)) n)) while (and (= sel 3) ironman)))
            (:down  (loop do (setf sel (mod (1+ sel) n))       while (and (= sel 3) ironman)))
            (:enter (unless (and (= sel 3) ironman) (return sel)))))))))

;;; ── Screen: Credits ──────────────────────────────────────────────
(defun screen-credits ()
  (cls%) (draw-box 2 8 22 73)
  (dolist (spec '((3  22 +yel+ t   "B U R D E N   O F   C O M M A N D")
                  (4  24 +gry+ nil "A WWI Trench Management Tycoon -- v4")
                  (6  11 +cyn+ t   "HISTORICAL CONTEXT")
                  (7  11 +wht+ nil "Set during the Third Battle of Ypres (Passchendaele), 1917.")
                  (8  11 +wht+ nil "The 11th East Lancashire Regiment is fictionalised but based")
                  (9  11 +wht+ nil "on the 'Pals Battalions' raised by Lord Derby in 1914-15.")
                  (11 11 +cyn+ t   "THE NUMBERS")
                  (12 11 +wht+ nil "The First World War killed approximately 17 million people.")
                  (13 11 +wht+ nil "A further 20 million were wounded. The war reshaped the world.")
                  (14 11 +wht+ nil "Every statistic was a person. Every person had a name.")
                  (16 11 +cyn+ t   "ON REMEMBRANCE")
                  (17 11 +gry+ nil "This game is offered as a small act of memory.")
                  (18 11 +gry+ nil "The men who served in the trenches of the Western Front")
                  (19 11 +gry+ nil "were ordinary people placed in extraordinary circumstances.")
                  (20 11 +gry+ nil "They endured. Many did not return. None should be forgotten.")
                  (22 28 +grn+ t   "Press any key to return.")))
    (destructuring-bind (r c col bold? text) spec
      (at% r c) (when bold? (bold%)) (fg% (symbol-value col))
      (write-string text) (rst%)))
  (flush%) (read-key))

;;; ── Screen: End ──────────────────────────────────────────────────
(defun screen-end ()
  (cls%)
  (let* ((men   (total-men))
         (maxm  (reduce #'+ (gs-squads *g*) :key #'sq-maxm :end (gs-squad-count *g*)))
         (pct   (if (zerop maxm) 0 (floor (* 100 men) maxm)))
         (score (calc-score)) (grade (score-grade score))
         (ei    (case (gs-over *g*) (#.+over-win+ 0) (#.+over-lose+ 1) (t 2)))
         (banners #("      ARMISTICE!  YOUR UNIT HAS BEEN RELIEVED.      "
                    "          YOUR COMPANY HAS BEEN ANNIHILATED.         "
                    "              THE MEN HAVE MUTINIED.                  "))
         (lines1 #("Captain Thorne -- you endured the unendurable."
                   "The mud of Flanders claimed them all."
                   "Despair consumed what artillery could not."))
         (col (aref (vector +grn+ +red+ +mag+) ei)))
    (at% 2 8) (with-color (col :bold)
      (write-string +tl+) (loop for i below 62 do (write-string +bh+)) (write-string +tr+))
    (at% 3 8) (with-color (col :bold) (format t "~A ~A ~A" +bv+ (aref banners ei) +bv+))
    (at% 4 8) (with-color (col :bold)
      (write-string +bl+) (loop for i below 62 do (write-string +bh+)) (write-string +br+))
    (at% 5 10) (with-color (col) (write-string (aref lines1 ei)))
    (at% 6 10) (with-color (+wht+)
      (case (gs-over *g*)
        (#.+over-win+  (format t "~D/~D men survived (~D%%)." men maxm pct))
        (#.+over-lose+ (format t "Fell on Week ~D, Turn ~D of ~D." (curr-week) (gs-turn *g*) (gs-maxt *g*)))
        (t             (format t "~D/~D men turned against their officers." men maxm))))
    (at% 7 10) (with-color (+cyn+ :bold) (format t "SCORE: ~D" score))
    (at% 7 27) (with-color (+yel+ :bold) (format t "GRADE: ~A" grade))
    (at% 7 39) (with-color ((dd-color (aref +diff-defs+ (gs-difficulty *g*))))
      (format t "[~A]" (dd-name (aref +diff-defs+ (gs-difficulty *g*)))))
    (at% 7 57) (with-color (+gry+) (format t "Rep:~D" (gs-hq-rep *g*)))
    (at% 9 10)  (with-color (+wht+ :bold) (write-string "CAMPAIGN REPORT"))
    (at% 10 10) (with-color (+gry+) (write-string (make-string 58 :initial-element #\-)))
    (at% 11 10) (with-color (+wht+)
      (format t " Turns: ~D/~D   Men: ~D/~D (~D%%)   Morale: ~D%% ~A"
              (gs-turn *g*) (gs-maxt *g*) men maxm pct (overall-mor) (mor-label (overall-mor))))
    (at% 12 10) (with-color (+wht+)
      (format t " Medals: ~D   Upgrades: ~D/~D   Rep: ~D/100   Rations: ~A"
              (gs-medals *g*) (popcount% (gs-upgrades *g*)) +upg-count+
              (gs-hq-rep *g*) (rd-name (aref +ration-defs+ (gs-ration-level *g*)))))
    (at% 14 10) (with-color (+cyn+ :bold) (write-string "SQUAD SUMMARY"))
    (at% 15 10) (with-color (+gry+) (write-string (make-string 58 :initial-element #\-)))
    (loop for i below (min (gs-squad-count *g*) 4)
          for s = (aref (gs-squads *g*) i)
          do (at% (+ 16 i) 10) (with-color (+wht+)
               (format t " ~8A  ~D/~D men  Mor:~3D%%  Raids:~2D  Lost:~2D  Wounds:~D"
                       (sq-name s) (sq-men s) (sq-maxm s) (sq-mor s)
                       (sq-raids-repelled s) (sq-men-lost s) (sq-wounds s))))
    (at% 21 10) (with-color (+gry+) (write-string "Press any key..."))
    (flush%) (read-key)))

;;; ── Game Logic ───────────────────────────────────────────────────
(defun rand-squad   () (aref (gs-squads *g*) (rng-range 0 (1- (gs-squad-count *g*)))))
(defun weakest-sq   () (reduce (lambda (a b) (if (< (sq-mor a) (sq-mor b)) a b))
                               (gs-squads *g*) :end (gs-squad-count *g*)))
(defun largest-sq   () (reduce (lambda (a b) (if (> (sq-men a) (sq-men b)) a b))
                               (gs-squads *g*) :end (gs-squad-count *g*)))

(defun record-history ()
  (let ((hi (mod (gs-res-hist-count *g*) +res-hist-len+)))
    (setf (aref (gs-res-hist *g*) hi +res-food+)  (gs-food  *g*)
          (aref (gs-res-hist *g*) hi +res-ammo+)  (gs-ammo  *g*)
          (aref (gs-res-hist *g*) hi +res-meds+)  (gs-meds  *g*)
          (aref (gs-res-hist *g*) hi +res-tools+) (gs-tools *g*))
    (incf (gs-res-hist-count *g*))))

(defun process-evq ()
  (let ((keep 0))
    (loop for i below (gs-ev-count *g*)
          for e = (aref (gs-evq *g*) i)
          do (cond
               ((and (plusp (gs-convoy-delayed *g*)) (= (ev-type e) +ev-supply+))
                (incf (ev-at e)) (decf (gs-convoy-delayed *g*))
                (setf (aref (gs-evq *g*) keep) e) (incf keep))
               ((/= (ev-at e) (gs-turn *g*))
                (setf (aref (gs-evq *g*) keep) e) (incf keep))
               (t (case (ev-type e)
                    (#.+ev-supply+
                     (setf-clamp (gs-food  *g*) (+ (gs-food  *g*) (ev-food  e)) 0 (food-cap))
                     (setf-clamp (gs-ammo  *g*) (+ (gs-ammo  *g*) (ev-ammo  e)) 0 (ammo-cap))
                     (setf-clamp (gs-meds  *g*) (+ (gs-meds  *g*) (ev-meds  e)) 0 50)
                     (setf-clamp (gs-tools *g*) (+ (gs-tools *g*) (ev-tools e)) 0 50)
                     (log-msg (format nil "Supply! +~Df +~Da +~Dm +~Dt"
                                      (ev-food e) (ev-ammo e) (ev-meds e) (ev-tools e)))
                     (when (and (gs-supply-req-pending *g*)
                                (= (gs-supply-req-pending *g*) (gs-turn *g*)))
                       (setf (gs-supply-req-pending *g*) nil)))
                    (#.+ev-reinforce+
                     (when (plusp (gs-squad-count *g*))
                       (let* ((sq (aref (gs-squads *g*) 0))
                              (a  (clamp (ev-men e) 0 (- (sq-maxm sq) (sq-men sq)))))
                         (incf (sq-men sq) a)
                         (log-msg (format nil "~D reinforcements join ~A!" a (sq-name sq))))))))))
    (setf (gs-ev-count *g*) keep)))

(defun apply-dispatch (idx comply)
  (let ((d (aref +hq-dispatches+ idx)))
    (setf (gs-dispatch-done *g*) (logior (gs-dispatch-done *g*) (ash 1 idx)))
    (if comply
        (progn
          (clampf (gs-agg *g*) (hd-cy-agg d) 5 95)
          (adjust-all-squads sq-mor (hd-cy-all-mor d))
          (setf-clamp (gs-ammo *g*) (+ (gs-ammo *g*) (hd-cy-ammo d)) 0 (ammo-cap))
          (setf-clamp (gs-food *g*) (+ (gs-food *g*) (hd-cy-food d)) 0 (food-cap))
          (when (hd-cy-force-raid d) (setf (sq-task (weakest-sq)) +task-raid+))
          (when (hd-cy-all-standby d)
            (do-squads (sq) (setf (sq-task sq) +task-standby+))
            (setf (gs-forced-standby-turns *g*) 1))
          (when (plusp (hd-cy-lose-men d))
            (let ((sq (largest-sq)))
              (setf-clamp (sq-men sq) (- (sq-men sq) (hd-cy-lose-men d)) 1 (sq-maxm sq))))
          (incf (gs-medals *g*) (hd-cy-medals d))
          (setf-clamp (gs-hq-rep *g*) (+ (gs-hq-rep *g*) (hd-cy-rep-delta d)) 0 100)
          (log-msg (format nil "COMPLY: ~A" (hd-comply-result d))))
        (progn
          (clampf (gs-agg *g*) (hd-df-agg d) 5 95)
          (adjust-all-squads sq-mor (hd-df-all-mor d))
          (setf-clamp (gs-hq-rep *g*) (+ (gs-hq-rep *g*) (hd-df-rep-delta d)) 0 100)
          (when (= idx 1) (setf (gs-convoy-delayed *g*) 3))
          (log-msg (format nil "DEFY: ~A" (hd-defy-result d)))))))

(defun process-historical ()
  (loop for i below (length +hist-events+)
        for he = (aref +hist-events+ i)
        when (and (zerop (logand (gs-hist-fired *g*) (ash 1 i)))
                  (>= (gs-turn *g*) (he-turn he)))
        do (setf (gs-hist-fired *g*) (logior (gs-hist-fired *g*) (ash 1 i)))
           (log-msg (he-text he))
           (clampf (gs-agg *g*) (he-agg-delta he) 5 95)
           (do-squads (sq)
             (clampf (sq-mor sq) (he-all-mor-delta he))
             (clampf (sq-fat sq) (he-all-fat-delta he)))
           (when (>= (he-set-weather he) 0) (setf (gs-weather *g*) (he-set-weather he))))
  (loop for i below (length +hq-dispatches+)
        for hd = (aref +hq-dispatches+ i)
        when (and (zerop (logand (gs-dispatch-done *g*) (ash 1 i)))
                  (>= (gs-turn *g*) (hd-turn hd))
                  (not (gs-dispatch-pending *g*)))
        do (setf (gs-dispatch-pending *g*) i)))

(defun apply-weather-effects ()
  (let* ((fx      (aref +weather-fx+ (gs-weather *g*)))
         (fat-add (truncate (+ (* (wfx-fat-per-turn fx)
                                  (dd-event-mul (aref +diff-defs+ (gs-difficulty *g*)))) 0.5))))
    (when (and (upg-has +upg-duckboards+)
               (member (gs-weather *g*) (list +weather-rain+ +weather-storm+)))
      (setf fat-add (floor fat-add 2)))
    (do-squads (sq) (clampf (sq-fat sq) fat-add))
    (clampf (gs-agg *g*) (wfx-agg-drift fx) 5 95)))

(defun apply-notable-passives ()
  (do-squads (sq)
    (when (plusp (sq-men sq))
      (when (find-notable sq +trait-cook+)
        (setf-clamp (gs-food *g*) (1+ (gs-food *g*)) 0 (food-cap)))
      (let ((med (find-notable sq +trait-medic+)))
        (when (and med (plusp (sq-wounds sq)))
          (decf (sq-wounds sq))
          (setf-clamp (gs-meds *g*) (1- (gs-meds *g*)) 0 50)))
      (when (find-notable sq +trait-scrounger+)
        (if (rng-bool 0.5)
            (setf-clamp (gs-ammo  *g*) (1+ (gs-ammo  *g*)) 0 (ammo-cap))
            (setf-clamp (gs-tools *g*) (1+ (gs-tools *g*)) 0 50)))
      (when (find-notable sq +trait-musician+)
        (clampf (sq-mor sq) 2)))))

(defun consume ()
  (let* ((fm (* (dd-food-mul  (aref +diff-defs+   (gs-difficulty    *g*)))
                (rd-food-mul  (aref +ration-defs+ (gs-ration-level  *g*)))))
         (fc (+ (truncate (+ (* (clamp (floor (total-men) 6) 1 99) fm) 0.5))
                (wfx-food-extra (aref +weather-fx+ (gs-weather *g*)))))
         (am (ad-ammo-mul (aref +ammo-defs+ (gs-ammo-policy *g*))))
         (ac (reduce (lambda (s sq)
                       (+ s (truncate (+ (* (td-ammo-cost (aref +task-defs+ (sq-task sq))) am) 0.5))))
                     (gs-squads *g*) :initial-value 0 :end (gs-squad-count *g*)))
         (mc (total-wounds)))
    (setf-clamp (gs-food *g*) (- (gs-food *g*) fc) 0 (food-cap))
    (setf-clamp (gs-ammo *g*) (- (gs-ammo *g*) ac) 0 (ammo-cap))
    (setf-clamp (gs-meds *g*) (- (gs-meds *g*) mc) 0 50)
    (let ((ration-mor (rd-mor-per-turn (aref +ration-defs+ (gs-ration-level *g*)))))
      (unless (zerop ration-mor) (adjust-all-squads sq-mor ration-mor)))
    (when (< (gs-food *g*) 15)
      (let ((mm (dd-morale-mul (aref +diff-defs+ (gs-difficulty *g*)))))
        (do-squads (sq) (clampf (sq-mor sq) (- (truncate (* 5 mm))))))
      (add-msg "CRITICAL: Food exhausted -- morale falling!"))
    (when (< (gs-ammo *g*) 10) (add-msg "WARNING: Ammunition nearly depleted!"))
    (when (and (<= (gs-meds *g*) 0) (plusp (total-wounds)))
      (add-msg "WARNING: No meds -- wounds untreated!"))))

(defun update-squads ()
  (let ((mm (dd-morale-mul (aref +diff-defs+ (gs-difficulty *g*)))))
    (do-squads (sq)
      (let* ((pm (if (sq-has-sgt sq) (pd-mul (aref +pers-defs+ (ss-pers (sq-sgt sq)))) 1.0))
             (td (aref +task-defs+ (sq-task sq))))
        (incf (sq-turns-alive sq))
        ;; fatigue
        (clampf (sq-fat sq) (truncate (* (td-fat-delta td) pm)))
        ;; morale
        (let* ((patrol-mul (if (= (sq-task sq) +task-patrol+)
                               (ad-patrol-mor-mul (aref +ammo-defs+ (gs-ammo-policy *g*))) 1.0))
               (mor-d (truncate (* (td-mor-delta td) pm patrol-mul))))
          (when (and (= (sq-task sq) +task-patrol+) (upg-has +upg-periscope+)) (incf mor-d 2))
          (when (upg-has +upg-dugout+) (incf mor-d 1))
          (when (and (= (sq-task sq) +task-patrol+) (< (gs-ammo *g*) 20))
            (decf mor-d (truncate (* 4 mm))))
          (clampf (sq-mor sq) mor-d))
        ;; resource gains from task
        (when (plusp (td-food-gain td))
          (setf-clamp (gs-food  *g*) (+ (gs-food  *g*) (td-food-gain td))  0 (food-cap)))
        (when (plusp (td-ammo-gain td))
          (setf-clamp (gs-ammo  *g*) (+ (gs-ammo  *g*) (td-ammo-gain td))  0 (ammo-cap)))
        (when (plusp (td-tools-gain td))
          (setf-clamp (gs-tools *g*) (+ (gs-tools *g*) (td-tools-gain td)) 0 50))
        ;; high fatigue morale drain
        (when (> (sq-fat sq) 80) (clampf (sq-mor sq) (- (truncate (* 4 mm)))))
        ;; forced standby
        (when (plusp (gs-forced-standby-turns *g*)) (setf (sq-task sq) +task-standby+))
        ;; wound death when no meds
        (when (and (plusp (sq-wounds sq)) (<= (gs-meds *g*) 0) (rng-bool 0.15) (> (sq-men sq) 1))
          (decf (sq-men sq)) (incf (sq-men-lost sq))
          (setf-clamp (sq-wounds sq) (1- (sq-wounds sq)) 0 (sq-men sq))
          (log-msg (format nil "~A Sq: 1 wounded man dies of infection." (sq-name sq))))
        ;; field hospital auto-heal
        (when (and (upg-has +upg-field-hosp+) (plusp (sq-wounds sq)) (plusp (gs-meds *g*)))
          (decf (sq-wounds sq)) (decf (gs-meds *g*)))
        ;; desertion
        (when (and (< (sq-mor sq) 10) (rng-bool 0.18) (> (sq-men sq) 1))
          (decf (sq-men sq)) (incf (sq-men-lost sq))
          (log-msg (format nil "~A Sq: desertion. Mor ~D." (sq-name sq) (sq-mor sq))))))
    (when (plusp (gs-forced-standby-turns *g*)) (decf (gs-forced-standby-turns *g*)))))

(defun has-supply-queued ()
  (loop for i below (gs-ev-count *g*)
        thereis (= (ev-type (aref (gs-evq *g*) i)) +ev-supply+)))

(defun push-ev (ev)
  (when (< (gs-ev-count *g*) +max-evq+)
    (setf (aref (gs-evq *g*) (gs-ev-count *g*)) ev)
    (incf (gs-ev-count *g*))))

(defun kill-notable (sq n)
  (when (and n (ns-alive n))
    (setf (ns-alive n) nil)
    (log-msg (format nil "~A of ~A Sq. has been killed in action." (ns-name n) (sq-name sq)))))

(defun random-events ()
  (let* ((em    (dd-event-mul  (aref +diff-defs+ (gs-difficulty *g*))))
         (mm    (dd-morale-mul (aref +diff-defs+ (gs-difficulty *g*))))
         (probs (make-array +revt-count+)))
    (loop for i below +revt-count+
          for (base-prob agg-div) = (aref +rand-probs+ i)
          for base = (if (plusp agg-div) (/ (gs-agg *g*) agg-div) base-prob)
          for wm   = (if (member i (list +revt-enemy-raid+ +revt-sector-assault+))
                         (wfx-raid-mul (aref +weather-fx+ (gs-weather *g*))) 1.0)
          do (setf (aref probs i) (* base em wm)))
    (when (upg-has +upg-obs-post+)
      (setf (aref probs +revt-sniper+)     (* (aref probs +revt-sniper+)     0.75)
            (aref probs +revt-enemy-raid+) (* (aref probs +revt-enemy-raid+) 0.75)))
    ;; ARTILLERY
    (when (rng-bool (aref probs +revt-artillery+))
      (let* ((sq  (rand-squad))
             (cas (rng-range 1 2)))
        (when (upg-has +upg-sandbag+) (setf cas (clamp (1- cas) 0 cas)))
        (when (> (sq-men sq) cas)
          (decf (sq-men sq) cas) (incf (sq-men-lost sq) cas) (incf (sq-wounds sq) cas)
          (clampf (sq-mor sq) (- (truncate (* cas 6 mm))))
          (log-msg (format nil "ARTILLERY! ~A Sq: ~D casualt~@*~[ies~;y~:;ies~], +~DW." (sq-name sq) cas cas))
          (when (rng-bool 0.25)
            (loop for j below (sq-notable-count sq)
                  for nb = (aref (sq-notables sq) j)
                  when (and (ns-alive nb) (rng-bool 0.4))
                  do (kill-notable sq nb) (return))))))
    ;; ENEMY RAID
    (when (rng-bool (aref probs +revt-enemy-raid+))
      (let* ((sq  (rand-squad))
             (res (+ (aref +raid-resist+ (sq-task sq))
                     (ad-raid-resist-add (aref +ammo-defs+ (gs-ammo-policy *g*))))))
        (when (upg-has +upg-lewis-nest+)                (incf res 0.15))
        (when (find-notable sq +trait-sharpshooter+)    (incf res 0.10))
        (if (rng-bool res)
            (progn
              (clampf (sq-mor sq) 6) (incf (sq-raids-repelled sq))
              (when (= (sq-raids-repelled sq) 2) (incf (gs-medals *g*)))
              (when (= (sq-raids-repelled sq) 4) (incf (gs-medals *g*)))
              (log-msg (format nil "~A Sq repelled enemy raid! Morale up." (sq-name sq))))
            (progn
              (when (> (sq-men sq) 1) (decf (sq-men sq)) (incf (sq-men-lost sq)) (incf (sq-wounds sq)))
              (clampf (sq-mor sq) (- (truncate (* 12 mm))))
              (log-msg (format nil "~A Sq: enemy raid broke through! 1 KIA." (sq-name sq)))))))
    ;; GAS
    (when (rng-bool (aref probs +revt-gas+))
      (let ((sq (rand-squad)))
        (if (>= (gs-meds *g*) 5)
            (progn (decf (gs-meds *g*) 5)
                   (log-msg (format nil "GAS ATTACK on ~A. Meds used (-5)." (sq-name sq))))
            (let ((cas (rng-range 1 2)))
              (setf-clamp (sq-men sq) (- (sq-men sq) cas) 0 (sq-maxm sq))
              (incf (sq-men-lost sq) cas) (incf (sq-wounds sq) cas)
              (log-msg (format nil "GAS ATTACK -- no meds! ~D man~:P lost." cas))))))
    ;; MAIL
    (when (rng-bool (aref probs +revt-mail+))
      (let ((sq (rand-squad)))
        (clampf (sq-mor sq) (rng-range 4 10))
        (log-msg (format nil "Mail from home cheers ~A Squad." (sq-name sq)))))
    ;; RATS
    (when (and (rng-bool (aref probs +revt-rats+)) (not (upg-has +upg-food-cache+)))
      (let ((lost (rng-range 2 9)))
        (setf-clamp (gs-food *g*) (- (gs-food *g*) lost) 0 (food-cap))
        (log-msg (format nil "Rats in the stores! ~D rations lost." lost))))
    ;; INFLUENZA
    (when (rng-bool (* (aref probs +revt-influenza+) (if (upg-has +upg-sump+) 0.5 1.0)))
      (let ((sq (rand-squad)))
        (when (> (sq-men sq) 1) (decf (sq-men sq)) (incf (sq-men-lost sq)) (incf (sq-wounds sq)))
        (log-msg (format nil "Influenza: ~A Sq loses 1 man." (sq-name sq)))))
    ;; SGT BREAKDOWN
    (when (rng-bool (aref probs +revt-sgt-breakdown+))
      (let ((sq (rand-squad)))
        (when (and (sq-has-sgt sq) (ss-ok (sq-sgt sq)))
          (setf (ss-ok (sq-sgt sq)) nil)
          (clampf (sq-mor sq) (- (truncate (* 8 mm))))
          (log-msg (format nil "~A has broken down. ~A Sq morale falls." (ss-name (sq-sgt sq)) (sq-name sq))))))
    ;; SUPPLY CONVOY
    (when (and (rng-bool (aref probs +revt-supply-convoy+)) (not (has-supply-queued)))
      (let* ((q   (if (>= (gs-hq-rep *g*) 70) 1 0))
             (eta (+ (gs-turn *g*) (rng-range 3 7))))
        (do-squads (sq)
          (when (find-notable sq +trait-runner+)
            (setf eta (clamp (1- eta) (1+ (gs-turn *g*)) (+ (gs-turn *g*) 8)))
            (return)))
        (push-ev (make-sev-s :at eta :type +ev-supply+
                   :food  (rng-range (if (= q 1) 15 10) (if (= q 1) 30 20))
                   :ammo  (rng-range (if (= q 1) 12  8) (if (= q 1) 22 15))
                   :meds  (rng-range (if (= q 1)  5  3) (if (= q 1) 10  7))
                   :tools (rng-range (if (= q 1)  4  2) (if (= q 1)  8  5))))
        (log-msg (format nil "HQ: Supply convoy en route. ETA ~D turns." (- eta (gs-turn *g*))))))
    ;; REINFORCE
    (when (rng-bool (aref probs +revt-reinforce+))
      (let ((eta (+ (gs-turn *g*) (rng-range 5 12))))
        (push-ev (make-sev-s :at eta :type +ev-reinforce+ :men (rng-range 1 3)))
        (log-msg (format nil "HQ: Reinforcements en route. ETA ~D turns." (- eta (gs-turn *g*))))))
    ;; SNIPER
    (when (rng-bool (aref probs +revt-sniper+))
      (let ((sq (rand-squad)))
        (when (and (> (sq-men sq) 1)
                   (member (sq-task sq) (list +task-standby+ +task-patrol+ +task-forage+)))
          (let ((ss (find-notable sq +trait-sharpshooter+)))
            (if (and ss (rng-bool 0.5))
                (log-msg (format nil "Sniper targeting ~A Sq -- ~A spots him first!" (sq-name sq) (ns-name ss)))
                (progn
                  (decf (sq-men sq)) (incf (sq-men-lost sq)) (incf (sq-wounds sq))
                  (clampf (sq-mor sq) (- (truncate (* 10 mm))))
                  (log-msg (format nil "Sniper! ~A Sq: 1 man hit." (sq-name sq)))))))))
    ;; FRATERNIZATION
    (when (and (rng-bool (aref probs +revt-fraternize+))
               (member (gs-weather *g*) (list +weather-clear+ +weather-fog+)))
      (do-squads (sq) (clampf (sq-mor sq) 6))
      (clampf (gs-agg *g*) -8 5 95)
      (log-msg "Brief fraternization across no-man's-land. Aggression falls."))
    ;; HERO
    (when (rng-bool (aref probs +revt-hero+))
      (let ((sq (rand-squad)))
        (clampf (sq-mor sq) 10) (incf (sq-raids-repelled sq))
        (log-msg (format nil "Heroism: a man in ~A Sq distinguishes himself." (sq-name sq)))))
    ;; FRIENDLY FIRE
    (when (rng-bool (aref probs +revt-friendly-fire+))
      (let ((sq (rand-squad)))
        (when (> (sq-men sq) 1) (decf (sq-men sq)) (incf (sq-men-lost sq)) (incf (sq-wounds sq)))
        (clampf (sq-mor sq) (- (truncate (* 15 mm))))
        (log-msg (format nil "Friendly fire! ~A Sq: 1 man lost." (sq-name sq)))))
    ;; ENEMY CACHE
    (when (rng-bool (aref probs +revt-cache+))
      (let ((f (rng-range 5 15)))
        (setf-clamp (gs-ammo *g*) (+ (gs-ammo *g*) f) 0 (ammo-cap))
        (log-msg (format nil "Patrol finds enemy cache! +~D ammo." f))))
    ;; FOOD SPOILAGE
    (when (and (rng-bool (aref probs +revt-food-spoil+)) (not (upg-has +upg-food-cache+)))
      (let ((lost (rng-range 3 12)))
        (setf-clamp (gs-food *g*) (- (gs-food *g*) lost) 0 (food-cap))
        (log-msg (format nil "Food spoilage! ~D rations lost." lost))))
    ;; AMMO DAMP
    (when (and (rng-bool (aref probs +revt-ammo-damp+))
               (not (upg-has +upg-munitions+))
               (member (gs-weather *g*) (list +weather-rain+ +weather-storm+)))
      (let ((lost (rng-range 3 10)))
        (setf-clamp (gs-ammo *g*) (- (gs-ammo *g*) lost) 0 (ammo-cap))
        (log-msg (format nil "Damp in the ammo crypt! ~D rounds lost." lost))))
    ;; WOUND HEAL
    (when (rng-bool (aref probs +revt-wound-heal+))
      (do-squads (sq)
        (when (and (plusp (sq-wounds sq)) (plusp (gs-meds *g*)))
          (decf (sq-wounds sq)) (decf (gs-meds *g*))
          (when (zerop (sq-wounds sq))
            (log-msg (format nil "~A Sq: all wounds treated." (sq-name sq))))
          (return))))
    ;; SECTOR ASSAULT
    (when (rng-bool (aref probs +revt-sector-assault+))
      (let ((sec (rng-range 0 3)))
        (clampf (aref (gs-sector-threat *g*) sec) (rng-range 15 30) 0 100)
        (log-msg (format nil "Enemy pressure rising in Sector ~A." (aref #("A" "B" "C" "D") sec)))))
    ;; sector threat drift
    (loop for i below 4
          for drift = (+ (if (> (gs-agg *g*) 60) 1 0) (if (< (gs-agg *g*) 30) -1 0))
          do (when (< i (gs-squad-count *g*))
               (when (member (sq-task (aref (gs-squads *g*) i))
                             (list +task-patrol+ +task-raid+))
                 (decf drift 2)))
             (clampf (aref (gs-sector-threat *g*) i) drift 0 100))
    (setf (gs-weather *g*) (weather-next (gs-weather *g*)))
    (clampf (gs-agg *g*) (- (rng-range 0 12) 6) 5 95)))

(defun check-over ()
  (cond
    ((>= (gs-turn *g*) (gs-maxt *g*)) (setf (gs-over *g*) +over-win+))
    ((zerop (total-men))              (setf (gs-over *g*) +over-lose+))
    (t (do-squads (sq)
         (when (>= (sq-mor sq) 5) (return-from check-over)))
       (setf (gs-over *g*) +over-mutiny+))))

(defun end-turn ()
  (when (gs-am *g*)
    (setf-clamp (gs-cmd-points *g*)
                (+ (gs-cmd-points *g*) 1 (if (upg-has +upg-signal-wire+) 1 0))
                0 (cp-max)))
  (record-history) (process-evq) (process-historical)
  (apply-weather-effects) (apply-notable-passives) (consume) (update-squads) (random-events)
  (incf (gs-turn *g*))
  (setf (gs-am *g*) (not (gs-am *g*)))
  (setf (gs-score *g*) (calc-score))
  (check-over))

;;; ── Input Handler ────────────────────────────────────────────────
(defun handle (key)
  (let ((ns (gs-squad-count *g*)) (no +task-count+))
    (if (gs-orders-mode *g*)
        (case key
          ((:left :up)    (setf (gs-osel *g*) (mod (+ (gs-osel *g*) (1- no)) no)))
          ((:right :down) (setf (gs-osel *g*) (mod (1+ (gs-osel *g*)) no)))
          (:enter
           (let ((sq (and (< (gs-sel *g*) ns) (aref (gs-squads *g*) (gs-sel *g*)))))
             (when sq
               (setf (sq-task sq) (aref +order-opts+ (gs-osel *g*)))
               (log-msg (format nil "Orders: ~A Sq -- ~A."
                                (sq-name sq) (td-name (aref +task-defs+ (sq-task sq)))))))
           (setf (gs-orders-mode *g*) nil))
          (:esc (setf (gs-orders-mode *g*) nil)))
        (case key
          ((:up :left)    (setf (gs-sel *g*) (mod (+ (gs-sel *g*) (1- ns)) ns)))
          ((:down :right) (setf (gs-sel *g*) (mod (1+ (gs-sel *g*)) ns)))
          (:space  (end-turn))
          (:O      (setf (gs-orders-mode *g*) t) (setf (gs-osel *g*) 0))
          (:C      (screen-command))
          (:R      (screen-resources))
          (:I      (screen-intel))
          (:D      (screen-dossier (gs-sel *g*)))
          (:Q      (return-from handle :quit))
          (:esc    (return-from handle :esc)))))
  nil)

;;; ── Main Entry Point ─────────────────────────────────────────────
(defun cleanup ()
  (cls%) (cur-on%)
  #-win32 (when *orig-termios* (sb-posix:tcsetattr 0 sb-posix:tcsanow *orig-termios*))
  #+win32 (raw-off)
  (format t "~%Burden of Command -- Thank you for playing.~%Auf Wiedersehen, Captain Thorne.~%~%")
  (finish-output))

(defun main ()
  (raw-on) (cur-off%)
  (tagbody
   main-menu
     (let ((choice (screen-main-menu)))
       (cond
         ((= choice 5) (go done))
         ((= choice 2) (screen-how-to-play)  (go main-menu))
         ((= choice 3) (screen-codex 0)      (go main-menu))
         ((= choice 4) (screen-credits)      (go main-menu))
         ((= choice 1)
          (let ((slot (screen-save-load 0)))
            (if (< slot 0) (go main-menu)
                (unless (load-slot slot) (add-msg "Failed to load.") (go main-menu)))))
         (t (new-game (screen-difficulty)))))
     ;; Game loop
     (loop while (zerop (gs-over *g*))
           do (render)
              (when (and (gs-dispatch-pending *g*) (>= (gs-dispatch-pending *g*) 0))
                (let* ((idx    (gs-dispatch-pending *g*))
                       (comply (screen-hq-dispatch idx)))
                  (setf (gs-dispatch-pending *g*) nil)
                  (apply-dispatch idx comply)))
              (let ((r (handle (read-key))))
                (case r
                  (:quit (return))
                  (:esc
                   (case (screen-pause-menu)
                     (0 nil)
                     (1 (screen-how-to-play))
                     (2 (let ((s (screen-save-load 1)))
                          (when (>= s 0)
                            (if (save-slot s) (add-msg "Game saved.") (add-msg "Save failed.")))))
                     (3 (let ((s (screen-save-load 0)))
                          (when (and (>= s 0) (not (load-slot s))) (add-msg "Load failed."))))
                     (4 (screen-codex 0))
                     (5 (screen-diary))
                     (6 (screen-dossier (gs-sel *g*)))
                     (7 (screen-upgrades))
                     (8 (return)))))))
     (unless (zerop (gs-over *g*)) (screen-end))
     (go main-menu)
   done)
  (cleanup))

(main)
