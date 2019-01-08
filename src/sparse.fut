
module type sparse = {
  type matrix a

  val dims : matrix -> (i32, i32)

  val fromList : (i32,i32) -> [((i32, i32), a)] -> matrix a
  val fromDense : a[][] -> matrix a
  val toDense : matrix a -> [[a]]

  val empty : (i32,i32) -> matrix a
  val diag : i32 -> a -> matrix a

  val update : matrix a -> i32 -> i32 -> a -> matrix a
  val get : matrix a -> i32 -> i32 -> a

  val transpose : matrix a -> matrix a
  val sparseFlatten : matrix a -> [((i32, i32), a)]

  val sparseMap : matrix a -> (a -> b) -> matrix b

  val add : matrix a -> matrix a -> matrix a
  val mul : matrix a -> matrix a -> matrix a
}
