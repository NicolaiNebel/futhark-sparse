module type MonoidEq = {
  type t
  val add:  t -> t -> t
  val eq:   t -> t -> bool
  val mul:  t -> t -> t
  val zero: t
}

module NumericMonoid(M : numeric): (MonoidEq with t = M.t) = {
  type t = M.t

  let add:(t -> t -> t)   = (M.+)
  let eq:(t -> t -> bool) = (M.==)
  let mul:(t -> t -> t)   = (M.*)
  let zero:t              = M.i8 0
}

module monoideq_i32 = NumericMonoid(i32)
module monoideq_f32 = NumericMonoid(f32)
