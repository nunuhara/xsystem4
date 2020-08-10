;; Copyright (C) 2019 Nunuhara Cabbage <nunuhara@haniwa.technology>
;;
;; This program is free software; you can redistribute it and/or modify
;; it under the terms of the GNU General Public License as published by
;; the Free Software Foundation; either version 2 of the License, or
;; (at your option) any later version.
;;
;; This program is distributed in the hope that it will be useful,
;; but WITHOUT ANY WARRANTY; without even the implied warranty of
;; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
;; GNU General Public License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with this program; if not, see <http://gnu.org/licenses/>.

(import (scheme base))
(import (chibi show))
(import (chibi show pretty))

(define (void) (when #f))

(define type/array-types
  (list type/array-int
        type/array-float
        type/array-string
        type/array-struct
        type/array-func-type
        type/array-bool
        type/array-long
        type/array-delegate))

(define type/ref-array-types
  (list type/ref-array-int
        type/ref-array-float
        type/ref-array-string
        type/ref-array-struct
        type/ref-array-func-type
        type/ref-array-bool
        type/ref-array-long
        type/ref-array-delegate))

(define (vm-value->scheme val type)
  (cond
   ((= type type/int) (vm-value-int val))
   ((= type type/bool) (not (zero? (vm-value-int val))))
   ((= type type/long) (vm-value-long val))
   ((= type type/float) (vm-value-float val))
   ((= type type/string) (vm-value-string val))
   ((= type type/struct) (get-page (vm-value-int val)))
   ((memv type type/array-types) (get-page (vm-value-int val)))
   ((memv type type/ref-array-types) (get-page (vm-value-int val)))
   (else 'unsupported-data-type)))

(define (iota n)
  (let loop ((n (- n 1)) (result '()))
    (if (< n 0)
        result
        (loop (- n 1) (cons n result)))))

(define (page->vector page)
  (list->vector (map (lambda (varno)
                       (let ((var (page-ref page varno)))
                         (vm-value->scheme/recursive (variable-vm-value var)
                                                     (variable-data-type var))))
                     (iota (page-nr-vars page)))))

(define (page->alist page)
  (map (lambda (varno)
         (let ((var (page-ref page varno)))
           (cons (variable-name var)
                 (vm-value->scheme/recursive (variable-vm-value var)
                                             (variable-data-type var)))))
       (iota (page-nr-vars page))))

(define (vm-value->scheme/recursive val type)
  (let ((scm-val (vm-value->scheme val type)))
    (if (page? scm-val)
        (cond
         ((= (page-type scm-val) page-type/array) (page->vector scm-val))
         (else (page->alist scm-val)))
        scm-val)))

(define (variable-value v)
  (vm-value->scheme/recursive (variable-vm-value v)
                              (variable-data-type v)))

(define (global name)
  (let ((v (get-global-variable name)))
    (if v
        (variable-value v)
        'no-such-variable)))

(define (local name)
  (let ((v (get-local-variable name)))
    (if v
        (variable-value v)
        'no-such-variable)))

(define (var name)
  (let ((v (local name)))
    (if (eqv? v 'no-such-variable)
        (global name)
        v)))

(define (print-page page)
  (for-each (lambda (v)
              (show #t (variable-name v) "\t = " (variable-value v) nl))
            (map (lambda (i) (page-ref page i)) (iota (page-nr-vars page)))))

(define (_dbg-optional args default fname)
  (cond ((null? args) default)
        ((not (null? (cdr args))) (error (string-append "Too many arguments to " fname) (+ 1 (length args))))
        (else (car args))))

(define (print-locals . rest)
  (let ((i (_dbg-optional rest 0 "PRINT-LOCALS")))
    (print-page (frame-page i))))

(define (print-variable name)
  (show #t (pretty (var name)))
  (void))

(define (set-breakpoint fun-or-addr . rest)
  (let ((handler (_dbg-optional rest #f "SET-BREAKPOINT")))
    (when (and handler (not (procedure? handler)))
      (error "Breakpoint handler must be a procedure"))
    (if (string? fun-or-addr)
        (set-function-breakpoint fun-or-addr handler)
        (set-address-breakpoint fun-or-addr handler))))

;; aliases

(define g global)
(define l local)
(define v variable)
(define bt backtrace)
(define pl print-locals)
(define p print-variable)
(define bp set-breakpoint)
