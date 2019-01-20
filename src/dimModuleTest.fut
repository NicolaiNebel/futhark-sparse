type matrix [n][m]= { Inds : [](i32,i32), Vals : []i32 , n : i32, m : i32}

let dim [n][m] (a : matrix[n][m]) = (n,m)

let main = let a : matrix[3][3] = {Inds=[],Vals=[], n=3, m=3}
           in dim a