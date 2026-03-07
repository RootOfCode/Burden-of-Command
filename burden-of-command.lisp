;; BURDEN OF COMMAND — A WWI Trench Management Tycoon
;; Run: sbcl --load burden-of-command.lisp

(defpackage #:boc
  (:use #:cl))
(in-package #:boc)

;;; ── ANSI / Terminal

(defvar *e* (format nil "~C" #\Escape))

(defun at    (r c) (format t "~A[~D;~DH" *e* r c)  (finish-output))
(defun cls   ()    (format t "~A[2J~A[H" *e* *e*)   (finish-output))
(defun ?cur- ()    (format t "~A[?25l"   *e*)        (finish-output))
(defun ?cur+ ()    (format t "~A[?25h"   *e*)        (finish-output))
(defun fg    (c)   (format t "~A[~Dm"    *e* c)      (finish-output))
(defun bd    ()    (format t "~A[1m"     *e*)        (finish-output))
(defun rst   ()    (format t "~A[0m"     *e*)        (finish-output))
(defun dim   ()    (format t "~A[2m"     *e*)        (finish-output))

;; Standard colour codes
(defconstant +BLK+ 30) (defconstant +RED+ 31) (defconstant +GRN+ 32)
(defconstant +YEL+ 33) (defconstant +BLU+ 34) (defconstant +MAG+ 35)
(defconstant +CYN+ 36) (defconstant +WHT+ 37) (defconstant +GRY+ 90)

(defun raw! ()
  (sb-ext:run-program "/bin/sh" '("-c" "stty raw -echo min 1 time 0")
                      :input t :output t :wait t))
(defun cook! ()
  (sb-ext:run-program "/bin/sh" '("-c" "stty sane")
                      :input t :output t :wait t))

(defun getch ()
  "Read one keypress. Returns char or :up/:down/:left/:right/:esc/:enter."
  (let ((c (read-char *standard-input* nil #\Nul)))
    (cond
      ((char= c #\Escape)
       (sleep 0.02)
       (let ((c2 (read-char-no-hang *standard-input* nil nil)))
         (if (and c2 (char= c2 #\[))
             (let ((c3 (read-char-no-hang *standard-input* nil nil)))
               (case c3 (#\A :up) (#\B :down) (#\C :right) (#\D :left) (t :esc)))
             :esc)))
      ((or (char= c #\Return) (char= c (code-char 13))) :enter)
      (t (char-downcase c)))))

;;  Utilities

(defun clamp (x a b) (max a (min b x)))

(defun pad (str w)
  "Pad or truncate plain string to exactly w visible chars."
  (let ((n (length str)))
    (if (>= n w) (subseq str 0 w)
        (concatenate 'string str (make-string (- w n) :initial-element #\Space)))))

(defun bar (v mx w)
  "ASCII progress bar."
  (let* ((f (round (* w (/ (float (clamp v 0 mx)) mx))))
         (e (- w f)))
    (concatenate 'string
                 (make-string f :initial-element #\█)
                 (make-string e :initial-element #\░))))

;; ── Data Structures

(defstruct sgt  name (pers :steadfast) (ok t))

(defstruct squad
  name (men 8) (maxm 8) (mor 70) sgt
  (task :standby) (fat 0) (sick 0))

(defstruct sched-ev (at 0) (type :none) data)

(defstruct gs
  ;; Time
  (turn 1) (maxt 84) (half :am)         ; 84 turns = 6 weeks × 7 days × 2
  ;; Environment
  (weather :clear) (agg 40)
  ;; Resources
  (food 80) (ammo 60) (meds 30) (tools 40)
  ;; Entities
  squads msgs evq
  ;; UI state
  (sel 0) (orders-mode nil) (osel 0)
  ;; End state  nil | :win | :lose | :mutiny
  (over nil))

(defvar *g* nil "Active game state.")

;;  Initialisation

(defun new-game ()
  (setf *g*
    (make-gs
     :squads
     (list
      (make-squad :name "Alpha"   :men 7 :maxm 8 :mor 68
                  :sgt (make-sgt :name "Sgt. Harris" :pers :brave)    :fat 25)
      (make-squad :name "Bravo"   :men 8 :maxm 8 :mor 72
                  :sgt (make-sgt :name "Sgt. Moore"  :pers :steadfast) :fat 15)
      (make-squad :name "Charlie" :men 5 :maxm 8 :mor 44
                  :sgt (make-sgt :name "Sgt. Lewis"  :pers :drunkard)  :fat 55)
      (make-squad :name "Delta"   :men 6 :maxm 8 :mor 61
                  :sgt (make-sgt :name "Sgt. Bell"   :pers :cowardly)  :fat 40))
     :msgs
     (list "Intelligence: Expect shelling tonight."
           "Supply convoy expected in ~3 turns."
           "ORDERS: Hold the line for 6 weeks."
           "Welcome, Captain Thorne. God help us.")
     :evq
     (list (make-sched-ev :at 3 :type :supply
                          :data '(:food 25 :ammo 20 :meds 8 :tools 5))))))

;;  Label / colour helpers

(defun morl  (m) (cond ((>= m 80) "EXCELLENT") ((>= m 65) "GOOD")
                       ((>= m 45) "FAIR")       ((>= m 25) "POOR") (t "CRITICAL")))
(defun morc  (m) (if (>= m 65) +GRN+ (if (>= m 45) +YEL+ +RED+)))

(defun taskstr (t_)
  (case t_ (:standby "STANDBY") (:patrol "PATROL") (:raid "RAID")
           (:repair "REPAIR")   (:rest "REST")      (t "???")))
(defun taskc (t_)
  (case t_ (:patrol +CYN+) (:raid +RED+) (:repair +YEL+) (:rest +GRN+) (t +WHT+)))

(defun persstr (p)
  (case p (:brave "Brave") (:cowardly "Cowardly")
          (:drunkard "Drunkard") (:steadfast "Steadfast") (t "???")))

(defun wstr (w)
  (case w (:clear "Clear ☀") (:rain "Rainy ☔") (:fog "Foggy ~")
          (:snow "Snowing") (:storm "Storm ⚡") (t "???")))
(defun wc (w)
  (case w (:clear +CYN+) (:rain +BLU+) (:fog +WHT+) (:storm +MAG+) (t +WHT+)))

(defun overall-mor ()
  (let ((sq (gs-squads *g*)))
    (if sq (round (/ (reduce #'+ sq :key #'squad-mor) (float (length sq)))) 0)))

(defun total-men ()
  (reduce #'+ (gs-squads *g*) :key #'squad-men))

(defun curr-week ()
  (1+ (floor (1- (gs-turn *g*)) 14)))

(defun datstr ()
  (let* ((days  (floor (1- (gs-turn *g*)) 2))
         (mns   #("Jan" "Feb" "Mar" "Apr" "May" "Jun"
                  "Jul" "Aug" "Sep" "Oct" "Nov" "Dec"))
         (mdays #(31 28 31 30 31 30 31 31 30 31 30 31))
         (m 0) (d days))
    (loop while (and (< m 11) (>= d (aref mdays m)))
          do (decf d (aref mdays m)) (incf m))
    (format nil "~2D ~A 1917" (1+ d) (aref mns m))))

(defun addmsg (msg)
  (push msg (gs-msgs *g*))
  (when (> (length (gs-msgs *g*)) 7)
    (setf (gs-msgs *g*) (subseq (gs-msgs *g*) 0 7))))

;;;  Layout Constants
;;
;;  80 columns × 23 rows
;;  Col 1 = left border  Col 44 = divider  Col 80 = right border
;;  Left  inner: cols  2 – 43  (42 chars)
;;  Right inner: cols 45 – 79  (35 chars)
;;
;;  Row  1  top border
;;  Row  2  title / status bar
;;  Row  3  split border (╠…╦…╣)
;;  Row  4  resource headers
;;  Rows 4–8  resources (left) + messages (right)
;;  Row  9  divider (╠…╬…╣)
;;  Row 10  SQUADS / MAP headers
;;  Rows 11–20  squad entries (left) + sector map (right)
;;  Row 21  close-right border (╠…╩…╣)
;;  Row 22  controls bar
;;  Row 23  bottom border

(defconstant +W+   80)
(defconstant +DIV+ 44)
(defconstant +LW+  42)   ; left inner width
(defconstant +RW+  35)   ; right inner width

;;  Low-level draw helpers

(defun vbar! (r)
  "Draw blue ║ at current position."
  (declare (ignore r))
  (fg +BLU+) (bd) (write-string "║") (rst))

(defun row! (r lfn rfn)
  "Draw one full row: border | left-content | border | right-content | border."
  (at r 1)   (vbar! r) (funcall lfn)
  (at r +DIV+) (vbar! r) (funcall rfn)
  (at r +W+) (vbar! r)
  (finish-output))

(defun hline! (r kind)
  "Draw a horizontal rule at row r.
   kind: :top :bot :full :split :join :close-r"
  (at r 1)
  (fg +BLU+) (bd)
  (flet ((span (lo hi mid-col mid-ch end-ch)
           (loop for c from lo to hi
                 do (write-string
                     (cond ((= c 1)       (string (first '(#\╠ #\╚ #\╔ #\╠ #\╠ #\╠))))
                           ((= c +W+)     end-ch)
                           ((= c mid-col) mid-ch)
                           (t "═"))))))
    (case kind
      (:top     (write-string (concatenate 'string "╔"
                                           (make-string (- +W+ 2) :initial-element #\═) "╗")))
      (:bot     (write-string (concatenate 'string "╚"
                                           (make-string (- +W+ 2) :initial-element #\═) "╝")))
      (:full    (write-string "╠")
                (loop repeat (- +W+ 2) do (write-string "═"))
                (write-string "╣"))
      (:split   (write-string "╠")
                (loop for c from 2 to (1- +W+) do
                  (write-string (if (= c +DIV+) "╦" "═")))
                (write-string "╣"))
      (:join    (write-string "╠")
                (loop for c from 2 to (1- +W+) do
                  (write-string (if (= c +DIV+) "╬" "═")))
                (write-string "╣"))
      (:close-r (write-string "╠")
                (loop for c from 2 to (1- +W+) do
                  (write-string (if (= c +DIV+) "╩" "═")))
                (write-string "╣"))))
  (rst) (finish-output))

;;  Render

(defun render-resources (g msgs)
  (flet ((res-row (r label val mx bc msg-idx)
           (row! r
             (lambda ()
               (bd) (fg +WHT+) (write-string (format nil " ~6A" label)) (rst)
               (fg bc) (write-string (format nil "[~A]" (bar val mx 10))) (rst)
               (write-string (format nil " ~3D/~3D" val mx))
               (write-string (make-string (max 0 (- +LW+ 24)) :initial-element #\Space)))
             (lambda ()
               (let ((msg (nth msg-idx msgs)))
                 (if msg
                     (progn (fg +CYN+) (write-string " › ") (rst)
                            (fg +WHT+) (write-string (pad msg (- +RW+ 3))) (rst))
                     (write-string (make-string +RW+ :initial-element #\Space))))))))
    (res-row 4 "Food:"  (gs-food  g) 100 +GRN+ 0)
    (res-row 5 "Ammo:"  (gs-ammo  g) 100 +YEL+ 1)
    (res-row 6 "Meds:"  (gs-meds  g)  50 +CYN+ 2)
    (res-row 7 "Tools:" (gs-tools g)  50 +MAG+ 3))
  ;; Morale row (row 8)
  (let ((m (overall-mor)))
    (row! 8
      (lambda ()
        (bd) (fg +WHT+) (write-string " Moral") (rst)
        (fg (morc m)) (write-string (format nil "[~A] ~3D%% ~9A" (bar m 100 8) m (morl m))) (rst)
        (write-string (make-string (max 0 (- +LW+ 29)) :initial-element #\Space)))
      (lambda ()
        (let ((msg (nth 4 msgs)))
          (if msg
              (progn (fg +CYN+) (write-string " › ") (rst)
                     (fg +WHT+) (write-string (pad msg (- +RW+ 3))) (rst))
              (write-string (make-string +RW+ :initial-element #\Space))))))))

(defun render-squads+map (g)
  (let* ((squads (gs-squads g))
         (sel    (gs-sel g))
         (om     (gs-orders-mode g))
         (osel   (gs-osel g))
         (orders '(:standby :patrol :raid :repair :rest))
         (agg    (gs-agg g))
         (map-lines
          (list
           "                                   "
           " ─────────────────────────────── "
           "  [A]  [B]  [C]  [D]   Sectors  "
           " ─────────────────────────────── "
           "    ╔══════════╗                 "
           "    ║   H.Q.   ║  ~~No Man's~~  "
           "    ╚══════════╝  ~~  Land   ~~  "
           "                                   "
           (format nil " Aggrn:[~A] ~D%%" (bar agg 100 9) agg)
           (format nil " ~A"
                   (concatenate 'string
                                (make-string (min 15 (floor agg 7)) :initial-element #\!)
                                (if (> agg 70) " ATTACK LIKELY" ""))))))
    (loop for r from 11 to 20
          for mi from 0
          do
          (let* ((sq-idx (floor (- r 11) 2))
                 (sq-sub (mod  (- r 11) 2))
                 (sq     (nth sq-idx squads))
                 (mline  (nth mi map-lines)))
            (row! r
              ;; ── LEFT: squad info ──
              (lambda ()
                (cond
                  ((null sq)
                   (write-string (make-string +LW+ :initial-element #\Space)))
                  ((= sq-sub 0)
                   ;; Name / men / morale / task
                   (let* ((s?  (= sq-idx sel))
                          (pfx (if s? " ▶ " "   "))
                          (mc  (morc (squad-mor sq)))
                          (tc  (taskc (squad-task sq))))
                     (when s? (bd))
                     (fg +WHT+) (write-string pfx) (rst)
                     (bd) (fg (if s? +YEL+ +WHT+))
                     (write-string (pad (squad-name sq) 7)) (rst)
                     (fg +GRY+) (write-string (format nil "~D/~D " (squad-men sq) (squad-maxm sq))) (rst)
                     (fg mc) (write-string (format nil "[~A]" (bar (squad-mor sq) 100 7))) (rst)
                     (write-string " ")
                     (fg tc) (bd) (write-string (pad (taskstr (squad-task sq)) 7)) (rst)
                     (write-string (make-string (max 0 (- +LW+ 35)) :initial-element #\Space))))
                  (t
                   ;; Sergeant / fatigue row — or orders picker
                   (cond
                     ((and om (= sq-idx sel))
                      (let* ((cur  (nth osel orders))
                             (prev (nth (mod (1- osel) (length orders)) orders))
                             (next (nth (mod (1+ osel) (length orders)) orders)))
                        (fg +YEL+) (bd) (write-string "  ► ") (rst)
                        (fg +GRY+) (write-string (pad (taskstr prev) 7)) (rst)
                        (write-string " ")
                        (fg +CYN+) (bd) (write-string (format nil "[~A]" (taskstr cur))) (rst)
                        (write-string " ")
                        (fg +GRY+) (write-string (pad (taskstr next) 7)) (rst)
                        (write-string (make-string (max 0 (- +LW+ 29)) :initial-element #\Space))))
                     (t
                      (let* ((s   (squad-sgt sq))
                             (sn  (if s (sgt-name s) "No Sgt."))
                             (ps  (if s (persstr (sgt-pers s)) ""))
                             (fat (squad-fat sq))
                             (fc  (if (< fat 40) +GRN+ (if (< fat 70) +YEL+ +RED+))))
                        (dim) (fg +GRY+)
                        (write-string (pad (format nil "   ~A (~A)" sn ps) 33))
                        (rst)
                        (write-string "Fat:")
                        (fg fc) (write-string (format nil "~2D%%" fat)) (rst)
                        (write-string (make-string (max 0 (- +LW+ 40)) :initial-element #\Space))))))))
              ;; ── RIGHT: sector map ──
              (lambda ()
                (fg +GRY+)
                (write-string (pad (or mline "") +RW+))
                (rst)))))))

(defun render ()
  (cls)
  (let* ((g    *g*)
         (msgs (reverse (gs-msgs g))))

    ;; Row 1: top border
    (hline! 1 :top)

    ;; Row 2: title / status
    (at 2 1) (vbar! 2)
    (fg +YEL+) (bd) (write-string " BURDEN OF COMMAND") (rst)
    (fg +GRY+) (write-string "  │  ") (rst)
    (fg +WHT+) (write-string (datstr)) (rst)
    (fg +GRY+) (write-string "  ") (rst)
    (fg (if (eq (gs-half g) :am) +CYN+ +MAG+))
    (write-string (if (eq (gs-half g) :am) "Morning" "Evening")) (rst)
    (fg +GRY+) (write-string "  │  ") (rst)
    (fg (wc (gs-weather g))) (write-string (wstr (gs-weather g))) (rst)
    (fg +GRY+) (write-string (format nil "  │  Wk ~D/6  │  T ~D/~D"
                                     (curr-week) (gs-turn g) (gs-maxt g))) (rst)
    (at 2 +W+) (vbar! 2) (finish-output)

    ;; Row 3: split divider
    (hline! 3 :split)

    ;; Rows 4–8: resources + messages
    (render-resources g msgs)

    ;; Row 9: join divider
    (hline! 9 :join)

    ;; Row 10: column headers
    (row! 10
      (lambda () (bd) (fg +WHT+) (write-string (pad " SQUADS" +LW+)) (rst))
      (lambda () (bd) (fg +WHT+) (write-string (pad " SECTOR MAP" +RW+)) (rst)))

    ;; Rows 11–20: squads + map
    (render-squads+map g)

    ;; Row 21: close-right divider
    (hline! 21 :close-r)

    ;; Row 22: controls
    (at 22 1) (vbar! 22)
    (fg +YEL+)
    (let ((ctrl (if (gs-orders-mode g)
                    " [←→] Cycle orders   [ENTER] Confirm   [ESC] Cancel"
                    " [SPC] End Turn  [O] Orders  [↑↓/←→] Select  [S] Save  [Q] Quit")))
      (write-string (pad ctrl (- +W+ 2))))
    (rst) (at 22 +W+) (vbar! 22) (finish-output)

    ;; Row 23: bottom border
    (hline! 23 :bot)

    (at 24 1) (finish-output)))

;;  Game Logic

(defun rng (p) (< (random 1.0) (float p)))

(defun rand-weather (w)
  (let ((r (random 10)))
    (nth r (list w w w w :rain :rain :fog :clear :storm :clear))))

(defun rand-squad ()
  (nth (random (length (gs-squads *g*))) (gs-squads *g*)))

(defun process-evq ()
  (let ((t_ (gs-turn *g*)) keep)
    (dolist (e (gs-evq *g*))
      (if (/= (sched-ev-at e) t_)
          (push e keep)
          (case (sched-ev-type e)
            (:supply
             (destructuring-bind (&key (food 0) (ammo 0) (meds 0) (tools 0))
                 (sched-ev-data e)
               (incf (gs-food  *g*) food)  (setf (gs-food  *g*) (clamp (gs-food  *g*) 0 100))
               (incf (gs-ammo  *g*) ammo)  (setf (gs-ammo  *g*) (clamp (gs-ammo  *g*) 0 100))
               (incf (gs-meds  *g*) meds)  (setf (gs-meds  *g*) (clamp (gs-meds  *g*) 0  50))
               (incf (gs-tools *g*) tools) (setf (gs-tools *g*) (clamp (gs-tools *g*) 0  50))
               (addmsg (format nil "Supply wagon! +~Dfood +~Dammo +~Dmeds +~Dtools"
                               food ammo meds tools))))
            (:reinforce
             (let ((sq (first (gs-squads *g*))) (n (sched-ev-data e)))
               (when sq
                 (incf (squad-men sq) n)
                 (setf (squad-men sq) (min (squad-men sq) (squad-maxm sq)))
                 (addmsg (format nil "~D reinforcements join ~A Squad!" n (squad-name sq)))))))))
    (setf (gs-evq *g*) keep)))

(defun random-events ()
  (let ((agg  (gs-agg  *g*))
        (meds (gs-meds *g*))
        (turn (gs-turn *g*)))

    ;; Artillery shelling
    (when (rng (/ agg 350.0))
      (let* ((sq  (rand-squad))
             (cas (1+ (random 2))))
        (when (> (squad-men sq) cas)
          (decf (squad-men sq) cas)
          (decf (squad-mor sq) (* cas 8))
          (setf (squad-mor sq) (clamp (squad-mor sq) 0 100))
          (addmsg (format nil "ARTILLERY! ~A Sq: ~D casualt~:[ies~;y~]."
                          (squad-name sq) cas (= cas 1))))))

    ;; Enemy raid
    (when (rng (/ agg 500.0))
      (let* ((sq      (rand-squad))
             (resist  (case (squad-task sq)
                        (:patrol .55) (:raid .80) (:standby .30) (:rest .10) (t .40))))
        (if (rng resist)
            (progn (incf (squad-mor sq) 6)
                   (setf (squad-mor sq) (clamp (squad-mor sq) 0 100))
                   (addmsg (format nil "~A Sq repelled enemy raid! Morale ↑" (squad-name sq))))
            (progn (when (> (squad-men sq) 1) (decf (squad-men sq)))
                   (decf (squad-mor sq) 12)
                   (setf (squad-mor sq) (clamp (squad-mor sq) 0 100))
                   (addmsg (format nil "~A Sq: raid broke through! 1 KIA." (squad-name sq)))))))

    ;; Gas attack
    (when (rng 0.035)
      (let ((sq (rand-squad)))
        (if (>= meds 5)
            (progn (decf (gs-meds *g*) 5)
                   (addmsg (format nil "GAS ATTACK — ~A Sector! Meds used (-5)." (squad-name sq))))
            (progn (let ((cas (1+ (random 2))))
                     (decf (squad-men sq) cas)
                     (setf (squad-men sq) (max 0 (squad-men sq))))
                   (addmsg "GAS ATTACK — No meds available. Men lost!")))))

    ;; Mail from home
    (when (rng 0.10)
      (let ((sq (rand-squad)))
        (incf (squad-mor sq) (+ 4 (random 7)))
        (setf (squad-mor sq) (clamp (squad-mor sq) 0 100))
        (addmsg (format nil "Mail from home cheers ~A Squad." (squad-name sq)))))

    ;; Rats eat rations
    (when (rng 0.07)
      (let ((lost (+ 2 (random 8))))
        (decf (gs-food *g*) lost)
        (setf (gs-food *g*) (max 0 (gs-food *g*)))
        (addmsg (format nil "Rats in the stores! ~D rations lost." lost))))

    ;; Trench foot / influenza
    (when (rng 0.04)
      (let ((sq (rand-squad)))
        (when (> (squad-men sq) 1) (decf (squad-men sq)))
        (addmsg (format nil "Influenza: ~A Sq loses 1 man." (squad-name sq)))))

    ;; Sergeant breakdown
    (when (rng 0.03)
      (let* ((sq  (rand-squad))
             (sgt (squad-sgt sq)))
        (when sgt
          (setf (sgt-ok sgt) nil)
          (decf (squad-mor sq) 8)
          (setf (squad-mor sq) (clamp (squad-mor sq) 0 100))
          (addmsg (format nil "~A has broken down! ~A Sq morale ↓."
                          (sgt-name sgt) (squad-name sq))))))

    ;; Schedule supply convoy
    (when (and (rng 0.08)
               (not (find :supply (gs-evq *g*) :key #'sched-ev-type)))
      (let* ((eta (+ turn 3 (random 5))))
        (push (make-sched-ev :at eta :type :supply
                             :data (list :food  (+ 10 (random 20))
                                         :ammo  (+ 10 (random 15))
                                         :meds  (+ 3  (random 8))
                                         :tools (+ 3  (random 6))))
              (gs-evq *g*))
        (addmsg (format nil "HQ: Supply convoy en route. ETA ~D turns." (- eta turn)))))

    ;; Occasional reinforcements
    (when (rng 0.04)
      (let* ((eta (+ turn 5 (random 8))))
        (push (make-sched-ev :at eta :type :reinforce :data (1+ (random 3)))
              (gs-evq *g*))
        (addmsg (format nil "HQ: Reinforcements en route. ETA ~D turns." (- eta turn)))))

    ;; Weather and aggression drift
    (setf (gs-weather *g*) (rand-weather (gs-weather *g*)))
    (incf (gs-agg *g*) (- (random 13) 6))
    (setf (gs-agg *g*) (clamp (gs-agg *g*) 5 95))))

(defun consume ()
  (let* ((squads (gs-squads *g*))
         (men    (reduce #'+ squads :key #'squad-men))
         (fc     (max 1 (floor men 6)))
         (ac     (reduce #'+ squads
                         :key (lambda (s)
                                (case (squad-task s) (:patrol 3) (:raid 6) (t 1))))))
    (decf (gs-food *g*) fc)  (setf (gs-food *g*) (max 0 (gs-food *g*)))
    (decf (gs-ammo *g*) ac)  (setf (gs-ammo *g*) (max 0 (gs-ammo *g*)))
    (when (< (gs-food *g*) 15)
      (dolist (s squads)
        (decf (squad-mor s) 5) (setf (squad-mor s) (clamp (squad-mor s) 0 100)))
      (addmsg "CRITICAL: Food nearly exhausted — morale falling!"))
    (when (< (gs-ammo *g*) 10)
      (addmsg "WARNING: Ammunition nearly depleted!"))))

(defun update-squads ()
  (dolist (sq (gs-squads *g*))
    (let* ((task (squad-task sq))
           (pers (if (squad-sgt sq) (sgt-pers (squad-sgt sq)) :steadfast))
           (pm   (case pers (:brave 1.2) (:cowardly 0.7) (:drunkard 0.85) (t 1.0))))
      (case task
        (:rest    (decf (squad-fat sq) (floor (* 15 pm)))
                  (incf (squad-mor sq) (floor (* 5  pm))))
        (:patrol  (incf (squad-fat sq) 10)
                  (incf (squad-mor sq) 3)
                  (when (< (gs-ammo *g*) 20) (decf (squad-mor sq) 4)))
        (:raid    (incf (squad-fat sq) 20)
                  (incf (squad-mor sq) (floor (* 4 pm))))
        (:repair  (incf (squad-fat sq) 5)
                  (incf (gs-tools *g*) 2)
                  (setf (gs-tools *g*) (clamp (gs-tools *g*) 0 50)))
        (:standby (decf (squad-fat sq) 5)))
      (setf (squad-fat sq) (clamp (squad-fat sq) 0 100))
      (when (> (squad-fat sq) 80)
        (decf (squad-mor sq) 4))
      (setf (squad-mor sq) (clamp (squad-mor sq) 0 100))
      ;; Desertion at critical morale
      (when (and (< (squad-mor sq) 10) (rng 0.18) (> (squad-men sq) 1))
        (decf (squad-men sq))
        (addmsg (format nil "~A Sq: a man has deserted. (Mor ~D)"
                        (squad-name sq) (squad-mor sq)))))))

(defun check-over ()
  (cond
    ((>= (gs-turn *g*) (gs-maxt *g*))
     (setf (gs-over *g*) :win))
    ((= (total-men) 0)
     (setf (gs-over *g*) :lose))
    ((every (lambda (s) (< (squad-mor s) 5)) (gs-squads *g*))
     (setf (gs-over *g*) :mutiny))))

(defun end-turn! ()
  (process-evq)
  (consume)
  (update-squads)
  (random-events)
  (incf (gs-turn *g*))
  (setf (gs-half *g*) (if (eq (gs-half *g*) :am) :pm :am))
  (check-over))

;;  Save / Load

(defun save! ()
  (ignore-errors
    (with-open-file (f "boc.sav" :direction :output :if-exists :supersede)
      (with-standard-io-syntax (print *g* f)))
    (addmsg "Game saved to boc.sav.")))

(defun load! ()
  (handler-case
    (progn
      (with-open-file (f "boc.sav" :direction :input)
        (with-standard-io-syntax (setf *g* (read f))))
      (addmsg "Game loaded from boc.sav."))
    (error ()
      (addmsg "No save file found."))))

;;  Input Handler

(defparameter *order-opts* '(:standby :patrol :raid :repair :rest))

(defun handle (key)
  "Process a keypress. Returns :quit to exit, otherwise nil."
  (let ((om  (gs-orders-mode *g*))
        (ns  (length (gs-squads *g*)))
        (no  (length *order-opts*)))
    (if om
        ;; ── Orders mode ──
        (case key
          ((:left :up)
           (setf (gs-osel *g*) (mod (1- (gs-osel *g*)) no)))
          ((:right :down)
           (setf (gs-osel *g*) (mod (1+ (gs-osel *g*)) no)))
          (:enter
           (let* ((sq  (nth (gs-sel *g*) (gs-squads *g*)))
                  (new (nth (gs-osel *g*) *order-opts*)))
             (when sq
               (setf (squad-task sq) new)
               (addmsg (format nil "~A Sq ordered: ~A." (squad-name sq) (taskstr new)))))
           (setf (gs-orders-mode *g*) nil))
          (:esc (setf (gs-orders-mode *g*) nil))
          (t nil))
        ;; ── Normal mode ──
        (case key
          (:up    (setf (gs-sel *g*) (mod (1- (gs-sel *g*)) ns)))
          (:down  (setf (gs-sel *g*) (mod (1+ (gs-sel *g*)) ns)))
          (:left  (setf (gs-sel *g*) (mod (1- (gs-sel *g*)) ns)))
          (:right (setf (gs-sel *g*) (mod (1+ (gs-sel *g*)) ns)))
          (#\Space (end-turn!))
          (#\o   (setf (gs-orders-mode *g*) t)
                 (setf (gs-osel *g*) 0))
          (#\s   (save!))
          (#\l   (load!))
          (#\q   :quit)
          (t nil)))))

;;  Screens

(defun intro-screen ()
  (cls)
  (at 2 10) (fg +YEL+) (bd)
  (format t "╔════════════════════════════════════════════════════════════╗~%")
  (at 3 10) (format t "║      B U R D E N   O F   C O M M A N D                    ║~%")
  (at 4 10) (format t "║           A  W W I  T r e n c h  T y c o o n              ║~%")
  (at 5 10) (format t "╚════════════════════════════════════════════════════════════╝~%")
  (rst)
  (at 7 5) (dim) (fg +GRY+)
  (write-string "  It is 1917. You are Captain Alistair Thorne on the Western Front.") (rst)
  (at 8 5) (dim) (fg +GRY+)
  (write-string "  Your company is exhausted, under-supplied, stuck in a muddy hellhole.") (rst)
  (at 9 5) (dim) (fg +GRY+)
  (write-string "  Higher command demands impossible offensives. Survive 6 weeks.") (rst)
  (at 11 5) (fg +CYN+) (bd) (write-string "CONTROLS:") (rst)
  (at 12 5) (fg +WHT+) (write-string "  [SPACE] End Turn          [O] Give Orders to Selected Squad") (rst)
  (at 13 5) (fg +WHT+) (write-string "  [↑↓ / ←→] Select Squad   [S] Save   [L] Load   [Q] Quit") (rst)
  (at 14 5) (fg +GRY+) (write-string "  In Orders mode: [←→] cycle, [ENTER] confirm, [ESC] cancel") (rst)
  (at 16 5) (fg +YEL+) (bd) (write-string "OBJECTIVES:") (rst)
  (at 17 5) (fg +WHT+) (write-string "  Maintain morale, manage resources, survive raids and shelling.") (rst)
  (at 18 5) (fg +WHT+) (write-string "  If morale collapses → mutiny. If all men die → defeat.") (rst)
  (at 19 5) (fg +GRN+) (write-string "  Hold on for 6 weeks and your unit will be relieved — Victory!") (rst)
  (at 21 20) (fg +GRN+) (bd) (write-string "Press any key to begin...") (rst)
  (finish-output)
  (getch))

(defun end-screen ()
  (cls)
  (let* ((out  (gs-over  *g*))
         (men  (total-men))
         (maxm (reduce #'+ (gs-squads *g*) :key #'squad-maxm))
         (pct  (floor (* 100 (/ men (float maxm))))))
    (at 6 12)
    (case out
      (:win
       (fg +GRN+) (bd)
       (format t "╔══════════════════════════════════════════════════════╗~%")
       (at 7 12) (format t "║      ARMISTICE!  YOUR UNIT HAS BEEN RELIEVED.       ║~%")
       (at 8 12) (format t "╚══════════════════════════════════════════════════════╝~%")
       (rst) (at 10 12) (fg +YEL+)
       (format t "Captain Thorne — you endured the unendurable.~%")
       (at 11 12)
       (format t "~D/~D men survived (~D%%). The Western Front will not forget them.~%"
               men maxm pct))
      (:lose
       (fg +RED+) (bd)
       (format t "╔══════════════════════════════════════════════════════╗~%")
       (at 7 12) (format t "║        YOUR COMPANY HAS BEEN ANNIHILATED.           ║~%")
       (at 8 12) (format t "╚══════════════════════════════════════════════════════╝~%")
       (rst) (at 10 12) (fg +RED+)
       (format t "The mud of Flanders claimed them all.~%")
       (at 11 12)
       (format t "Fell on Week ~D, Turn ~D." (curr-week) (gs-turn *g*)))
      (:mutiny
       (fg +MAG+) (bd)
       (format t "╔══════════════════════════════════════════════════════╗~%")
       (at 7 12) (format t "║           THE MEN HAVE MUTINIED.                    ║~%")
       (at 8 12) (format t "╚══════════════════════════════════════════════════════╝~%")
       (rst) (at 10 12) (fg +MAG+)
       (format t "Despair consumed what artillery could not.~%")
       (at 11 12)
       (format t "~D/~D men turned against their officers.~%" men maxm)))
    (rst)
    (at 14 12) (fg +WHT+) (write-string "Press any key to exit...") (rst)
    (finish-output)
    (getch)))

;;  Main Loop

(defun main ()
  (handler-case
    (progn
      (raw!)
      (?cur-)
      (intro-screen)
      (new-game)
      (loop
        (when (gs-over *g*)
          (end-screen)
          (return))
        (render)
        (let ((result (handle (getch))))
          (when (eq result :quit) (return))))
      (cls) (?cur+) (cook!)
      (format t "~%Burden of Command — Thank you for playing.~%Auf Wiedersehen, Captain Thorne.~%~%")
      (finish-output))
    (error (e)
      (?cur+) (cook!)
      (format *error-output* "~%Fatal error: ~A~%~%" e)
      (sb-debug:print-backtrace)
      (finish-output))))

;;;  Entry Point ;;;

(main)
