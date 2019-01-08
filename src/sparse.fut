module type sparse = {
    type matrix a

    val dims : matrix -> (i32, i32)

    val fromList : [((i32, i32), t)] -> matrix
    val toDense : matrix a -> [[a]]

    val empty : matrix a
    val diag : i32 -> a -> matrix a

    val update : matrix -> i32 -> i32 -> a -> matrix a

    val transpose : matrix a -> matrix a
    val flatten : matrix a -> [((i32, i32), a)]

    val map : matrix a -> (a -> b) -> matrix b

    val add : matrix a -> matrix a -> matrix a
    val mul : matrix a -> matrix a -> matrix a
}
