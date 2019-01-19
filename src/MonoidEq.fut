module type MonoidEq = {
  type t
  val add: t -> t -> t
  val eq: t -> t -> bool
  val mul: t -> t -> t
  val ne: t
}
