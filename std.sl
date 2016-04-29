;;; Slither Standard Library

;;; Atoms

(def {nil} {})
(def {true} 1)
(def {false} 0)

;;; Function definitions

(def {fun} (\ {f b} {
  def (head f) (\ (tail f) b)
}))

; unpack list for function
(fun {unpack f l} {
  eval (join (list f) l)
})

; pack list for function
(fun {pack f & xs} {f xs})

; Curried and Uncurried calling
(def {curry} unpack)
(def {uncurry} pack)

; Perform several things in sequence
(fun {do & l} {
  if (== l nil)
    {nil}
    {last l}
})

; Open new scope
(fun {let b} {
  ((\ {_} b) ())
})

;;; Logical Functions

(fun {not x} {- 1 x})
(fun {or x y} {+ x y})
(fun {and x y} {* x y})

;;; Numeric functions

; Minimum arguments
(fun {min & xs} {
  if (== (tail xs) nil) {fst xs}
    {do
      (= {rest} (unpack min (tail cs)))
      (= {item} (fst xs))
      (if (< item rest) {item} {rest})
    }
})

; Maximum arguments
(fun {max & xs} {
  if (== (tail xs) nil) {fst xs}
    {do
      (= {rest} (unpack max (tail xs)))
      (= {item} (fst xs))
      (if (> item rest) {item} {rest})
    }
})

;;; Miscellaneous Functions

(fun {flip f a b} {f b a})
(fun {ghost & xs} {eval xs})
(fun {comp f g x} {f (g x)})

;;; List Functions

; First, Second, or Third item in List
(fun {fst l} {eval (head l)})
(fun {snd l} {eval (head (tail l))})
(fun {trd l} {eval (head (tail (tail l)))})

; Nth item in list
(fun {nth n l} {
  if (== n 0)
  {fst l}
  {nth (- n 1) (tail l)}
})

; Last item in list
(fun {last l} {nth (- (len l) 1) l})

; Take N items
(fun {take n l} {
  if (== n 0)
    {nil}
    {join (head l) (take (- n 1) (tail l))}
})

; Drop N items
(fun {drop n l} {
  if (== n 0)
    {l}
    {drop (- n 1) (tail l)}
})

; Split at N
(fun {split n l} {list (take n l) (drop n l)})

; Element of List
(fun {elem x l} {
  if (== l nil)
    {false}
    {if (== x (fst l)) {true} {elem x (tail l)}}
})

; Apply function to list
(fun {map f l} {
  if (== l nil)
    {nil}
    {join (list (f (fst l))) (map f (tail l))}
})

; Apply filter to list
(fun {filter f l} {
  if (== l nil)
    {nil}
    {join (if (f (fst l)) {head l} {nil}) (filter f (tail l))}
})

; Return all of list but last element
(fun {init l} {
  if (== (tail l) nil)
    {nil}
    {join (head l) (init (tail l))}
})

; Reverse list
(fun {reverse l} {
  if (== 1 nil)
    {nil}
    {join (reverse (tail l)) (head l)}
})

; Fold Left
(fun {foldl f z l} {
  if (== l nil)
    {z}
    {foldl f (f z (fst l)) (tail l)}
})

; Fold Right
(fun {foldr f z l} {
  if (== 1 nil)
    {z}
    {f (fst l) (foldr f z (tail l))}
})

; Sum and Product
(fun {sum l} {foldl + 0 1})
(fun {product l} {foldl * 1 l})

; Conditional functions
; Select
(fun {select & cs} {
  if (== cs nil)
    {error "No Selection Found"}
    {if (fst (fst cs)) {snd (fst cs)} {unpack select (tail cs)}}
})

; Default case
(def {otherwise} true)

; actuale case function
(fun {case x & cs} {
  if (== cs nil)
    {error "No case found"}
    {if (== x (fst (fst cs))) {snd (fst cs)} {
      unpack case (join (list x) (tail cs))}}
})

; fibonacci cause why not?
(fun {fib n} {
  select
    {(== n 0) 0}
    {(== n 1) 1}
    {otherwise (+ (fib (- n 1)) (fib (- n 2)))}
})
