import "lib/github.com/diku-dk/sorts/merge_sort"
import "lib/github.com/diku-dk/segmented/segmented"
import "futlib/math"

import "MonoidEq"

module spCoord(M: MonoidEq) = {
  type matrix = { Inds : [](i32,i32), Vals : []M.t, Dims : (i32,i32) }

let fromList (dim : (i32,i32)) (l : []((i32,i32),M.t)) : matrix =
  let listVals = filter (\(_,v) -> ! (M.eq v M.zero)) l
  let (inds,vals) = unzip listVals
  in {Inds = inds, Vals = vals, Dims = dim}

--General solution
let fromDense [m][n] (dense : [m][n]M.t) =
  let inds = flatten <| map (\i -> map (\j -> (i,j)) (iota n)) <| iota m
  let denseInds = zip inds <| flatten dense
  in fromList (m,n) denseInds


let toDense (mat : matrix) =
  let dim = mat.Dims
  let dense = replicate (dim.1*dim.2) M.zero
  let inds = map (\(i,j) -> i*dim.2+j) mat.Inds
  let res = scatter dense inds mat.Vals
  in unflatten dim.1 dim.2 res

let empty x y : matrix = {Inds = [], Vals=[], Dims=(x,y)}


let diag (size :i32) (el : M.t) : matrix =
  let (inds, vals) = unzip <| map (\i -> ((i,i),el)) (iota size)
  in {Inds = inds, Vals = vals, Dims= (size,size)}


let find_idx_first 'v [n] (e:v)  (eq : v -> v -> bool) (xs:[n]v) : i32 =
  let es = map2 (\x i -> if eq x e then i else n) xs (iota n)
  let res = reduce i32.min n es
  in if res == n then -1 else res

let update (mat : matrix) i j (el : M.t) : matrix =
  if i>=mat.Dims.1 && j>=mat.Dims.2 || i<0 || j<0
  then mat
  else let ind = find_idx_first (i,j) (==) mat.Inds in
       if ind != -1
       then let vals = copy mat.Vals
            in {Inds = mat.Inds, Vals = scatter vals [ind] [el], Dims=mat.Dims}
       else {Inds = mat.Inds++[(i,j)], Vals = mat.Vals++[el], Dims=mat.Dims}

let get (mat : matrix) i j : M.t =
  let ind = find_idx_first (i,j) (==) mat.Inds
  in if ind == (-1)
     then M.zero
     else unsafe(mat.Vals[ind])

let getDims (mat : matrix) : (i32,i32) = mat.Dims

let transpose (mat : matrix) : matrix =
  let inds = map (\(i,j) -> (j,i)) mat.Inds
  in {Inds = inds, Vals = mat.Vals, Dims = (mat.Dims.2,mat.Dims.1)}

let toListCoord (mat : matrix) : []((i32,i32),M.t) =
  zip mat.Inds mat.Vals

let sparseMap (mat : matrix) fun : matrix =
  {Inds = mat.Inds, Vals= map fun mat.Vals, Dims=mat.Dims}

let elementwise (mat0 : matrix) (mat1 : matrix) fun (ne : M.t) : matrix =
  if mat0.Dims == mat1.Dims
  then let mat = (zip mat0.Inds mat0.Vals) ++ (zip mat1.Inds mat1.Vals)
       let sort = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if i0==i1 then j0<=j1 else i0 <= i1) mat
       let flags = map2 (\(ind0,_) (ind1,_) -> ind0!=ind1) sort (rotate (-1) sort)
       let res = segmented_reduce (\(_,v0) (i1,v1) -> (i1,fun v0 v1)) ((0,0),ne) flags sort
       let (inds,vals) = unzip <| filter (\(_,v) -> ! (M.eq v M.zero)) res
       in {Inds = inds, Vals = vals, Dims = mat0.Dims}
  else empty 0 0

let matMult (sort0 : []((i32,i32),M.t)) count0 (sort1 : []((i32,i32),M.t)) count1 mul add i j : ((i32,i32),M.t) =
  let part0 = sort0[count0[i]:count0[i+1]]
  let part1 = sort1[count1[j]:count1[j+1]]
  let is = map (\(ind,_) -> ind.1) part1
  let res = map (\((_,j),v) : M.t -> let ind = find_idx_first j (==) is
                               in if ind == -1 then M.zero
                                  else mul v (unsafe( part1[ind] ).2)
                ) part0
  in ((i,j),reduce add M.zero res)

let mulFun (mat0 : matrix) (mat1 : matrix) (mul: M.t -> M.t -> M.t) (add: M.t -> M.t -> M.t) : matrix =
  if mat0.Dims.2 == mat1.Dims.1
  then let sort0 = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if i0==i1 then j0<=j1 else i0 <= i1) (zip mat0.Inds mat0.Vals)
       let ptr0 = let (inds,flag) = unzip <| map2 (\((ind0,_),_) ((ind1,_),_) -> (ind0,ind0!=ind1)) sort0 (rotate (-1) sort0)
             let flagi = zip inds <| replicate (length flag) 1
             let (inds1,count) =unzip <| segmented_reduce (\(_,i) (ind,v)-> (ind,i+v)) (0,0) flag flagi
             let row_lens= scatter (replicate mat0.Dims.1 0) inds1  count
             in scan (+) 0 <| [0] ++ row_lens
       let sort1 = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if j0==j1 then i0<=i1 else j0 <= j1) (zip mat1.Inds mat1.Vals)
       let ptr1 = let (inds,flag) = unzip <| map2 (\((_,ind0),_) ((_,ind1),_) -> (ind0,ind0!=ind1)) sort1 (rotate (-1) sort1)
             let flagi = zip inds <| replicate (length flag) 1
             let (inds1,count) =unzip <| segmented_reduce (\(_,i) (ind,v)-> (ind,i+v)) (0,0) flag flagi
             let col_lens= scatter (replicate mat1.Dims.2 0) inds1 count
             in scan (+) 0 <| [0] ++ col_lens
       let dense = expand (\_ -> mat1.Dims.2) (matMult sort0 ptr0 sort1 ptr1 mul add) (iota mat0.Dims.1)
       let (inds,vals) =unzip <| filter (\(_,v) -> ! (M.eq v M.zero)) dense
       in {Inds = inds, Vals = vals, Dims = (mat0.Dims.1,mat1.Dims.2)}
  else empty 0 0

let mul (mat0 : matrix) (mat1 : matrix) : matrix =
  mulFun mat0 mat1 (M.mul) (M.add)
}

------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------
------------------------------------------------------------------------------------------
--With array sizes
module spCoordSized(M: MonoidEq) = {
  type matrix [x][y]= { Inds : [](i32,i32), Vals : []M.t, _x : [x]u8 , _y : [y]u8}

let fromList ((x,y) : (i32,i32)) (l : []((i32,i32),M.t)) : matrix [x][y]=
  let (inds,vals) = unzip l
  in {Inds = inds, Vals = vals, _x=(replicate x (u8.i32 0)), _y=(replicate y (u8.i32 0))}

--General solution
let fromDense [m][n] (dense : [m][n]M.t) =
  let inds = flatten <| map (\i -> map (\j -> (i,j)) (iota n)) <| iota m
  let denseInds = zip inds <| flatten dense
  let listVals = filter (\(_,v) -> ! (M.eq v M.zero)) denseInds
  in fromList (m,n) listVals


let toDense [x][y] (mat : matrix[x][y]) : [x][y]M.t =
  let dense = replicate (x*y) M.zero
  let inds = map (\(i,j) -> i*y+j) mat.Inds
  let res = scatter dense inds mat.Vals
  in unflatten x y res

let empty (x:i32) (y:i32) : matrix[x][y]= {Inds = [], Vals = [], _x=(replicate x (u8.i32 0)), _y=(replicate y (u8.i32 0))}


let diag (size :i32) (el : M.t) : matrix[size][size] =
  let (inds, vals) = unzip <| map (\i -> ((i,i),el)) (iota size)
  in {Inds = inds, Vals = vals, _x=(replicate size (u8.i32 0)), _y=(replicate size (u8.i32 0))}


let find_idx_first 'v [n] (e:v)  (eq : v -> v -> bool) (xs:[n]v) : i32 =
  let es = map2 (\x i -> if eq x e then i else n) xs (iota n)
  let res = reduce i32.min n es
  in if res == n then -1 else res

let update [x][y] (mat : matrix [x][y]) i j (el : M.t) : matrix[x][y] =
  if i>=x && j>=y || i<0 || j<0
  then mat
  else let ind = find_idx_first (i,j) (==) mat.Inds in
       if ind != -1
       then let vals = copy mat.Vals
            in {Inds = mat.Inds, Vals = scatter vals [ind] [el], _x=mat._x, _y=mat._y}
       else {Inds = mat.Inds++[(i,j)], Vals = mat.Vals++[el], _x=mat._x, _y=mat._y}

let get [n][m] (mat : matrix[n][m]) i j : M.t =
  if i<n && i>=0 && j<m && j>=0
  then let ind = find_idx_first (i,j) (==) mat.Inds
       in if ind == (-1)
          then M.zero
          else unsafe(mat.Vals[ind])
  else M.zero -- should be an error

let getDims [x][y] (_ : matrix[x][y]) = (x,y)

let transpose [x][y] (mat : matrix[x][y]) : matrix[x][y] =
  let inds = map (\(i,j) -> (j,i)) mat.Inds
  in {Inds = inds, Vals = mat.Vals, _x=mat._y, _y=mat._x}

let toListCoord [x][y] (mat : matrix[x][y]) : []((i32,i32),M.t) =
  zip mat.Inds mat.Vals

let sparseMap [x][y] (mat : matrix[x][y]) fun : matrix[x][y] =
  {Inds = mat.Inds, Vals= map fun mat.Vals, _x=mat._x, _y=mat._y}

let elementwise [x][y] (mat0 : matrix[x][y]) (mat1 : matrix[x][y]) fun (ne : M.t) : matrix[x][y] =
  let mat = (zip mat0.Inds mat0.Vals) ++ (zip mat1.Inds mat1.Vals)
  let sort = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if i0==i1 then j0<=j1 else i0 <= i1) mat
  let flags = map2 (\(ind0,_) (ind1,_) -> ind0!=ind1) sort (rotate (-1) sort)
  let res = segmented_reduce (\(_,v0) (i1,v1) -> (i1,fun v0 v1)) ((0,0),ne) flags sort
  let (inds,vals) = unzip <| filter (\(_,v) -> ! (M.eq v M.zero)) res
  in {Inds = inds, Vals = vals, _x=mat0._x, _y=mat0._y}


let matMult (sort0 : []((i32,i32),M.t)) count0 (sort1 : []((i32,i32),M.t)) count1 mul add i j : ((i32,i32),M.t) =
  let part0 = sort0[count0[i]:count0[i+1]]
  let part1 = sort1[count1[j]:count1[j+1]]
  let is = map (.1.1) part1
  let res = map (\((_,j),v) : M.t -> let ind = find_idx_first j (==) is
                               in if ind == -1 then M.zero
                                  else mul v (unsafe( part1[ind] ).2)
                ) part0
  in ((i,j),reduce add M.zero res)

let mulFun [x][y][z] (mat0 : matrix[x][y]) (mat1 : matrix[y][z]) (mul: M.t -> M.t -> M.t) (add: M.t -> M.t -> M.t) : matrix[x][z] =
  let sort0 = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if i0==i1 then j0<=j1 else i0 <= i1) (zip mat0.Inds mat0.Vals)
  let ptr0 = let (inds,flag) = unzip <| map2 (\((ind0,_),_) ((ind1,_),_) -> (ind0,ind0!=ind1)) sort0 (rotate (-1) sort0)
             let flagi = zip inds <| replicate (length flag) 1
             let (inds1,count) =unzip <| segmented_reduce (\(_,i) (ind,v)-> (ind,i+v)) (0,0) flag flagi
             let row_lens= scatter (replicate x 0) inds1  count
             in scan (+) 0 <| [0] ++ row_lens
  let sort1 = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if j0==j1 then i0<=i1 else j0 <= j1) (zip mat1.Inds mat1.Vals)
  let ptr1 = let (inds,flag) = unzip <| map2 (\((_,ind0),_) ((_,ind1),_) -> (ind0,ind0!=ind1)) sort1 (rotate (-1) sort1)
             let flagi = zip inds <| replicate (length flag) 1
             let (inds1,count) =unzip <| segmented_reduce (\(_,i) (ind,v)-> (ind,i+v)) (0,0) flag flagi
             let col_lens= scatter (replicate z 0) inds1 count
             in scan (+) 0 <| [0] ++ col_lens
  let dense = expand (\_ -> y) (matMult sort0 ptr0 sort1 ptr1 mul add) (iota x)
  let (inds,vals) = unzip <| filter (\(_,v) -> ! (M.eq v M.zero)) dense
  in {Inds = inds, Vals = vals, _x=mat0._x, _y=mat1._y}

let mul [x][y][z] (mat0 : matrix[x][y]) (mat1 : matrix[y][z]) : matrix[x][z] =
  mulFun mat0 mat1 (M.mul) (M.add)
}