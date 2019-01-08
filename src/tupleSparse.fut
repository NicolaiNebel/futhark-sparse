import "lib/github.com/diku-dk/sorts/merge_sort"
import "lib/github.com/diku-dk/segmented"

type matrix a = {(Inds : [](i32,i32)), (Vals : []a), (Dims : (i32,i32))}

--General solution
let fromDense 'a (ne : a) [m][n] (dense : [m][n]a) =
  let inds = flatten map (\i -> map (\j -> (i,j)) (iota n)) iota m
  let denseInds = zip inds (flatten dense)
  let listVals = filter (\(_,val) -> val!=ne) denseInds
  in fromList (m,n) listVals

let fromList (dim : (i32,i32)) (l : []((i32,i32),a)) : matrix a =
  let (inds,vals) = unzip l
  in {Inds = inds, Vals = vals, Dims = dim}


let toDense 'a (ne : a) (mat : matrix a) =
  let dim = mat.Dims
  let dense = replicate ne (dim.1*dim.2)
  let inds = map (\((i,j),_) -> i+j*dim.1) mat.Inds
  let res = scatter dense inds mat.Vals
  in unflatten dim.1 dim.2 res

let empty (dim : (i32,i32)) : matrix a= {Inds = [], Vals=[], Dims=dim}


let diag (size :i32) (el : a) : matrix a =
  let (inds, vals) = unzip <| map (\i -> ((i,i),el)) (iota size)
  in {Inds = inds, Vals = vals, Dims= (size,size)}


let find_idx_first 'a [n] (e:a) (xs:[n]a) : i32 =
  let es = map2 (\x i -> if x==e then i else n) xs (iota n)
  let res = reduce i32.min n es
  in if res == n then -1 else res

let update 'a (mat : matrix a) i j (el : a) : matrix a =
  if i>=mat.Dims.1 && j>=mat.Dims.2
  then mat
  else let ind = find_idx_first (i,j) mat.Inds in
       if ind != -1
       then scatter a [ind] [el]
       else {Inds = mat.Inds++[(i,j)], Vals = mat.Vals++[el], Dims=mat.Dims}

let get 'a (mat : matrix a) (ne : a) i j : a =
  let ind = find_idx_first (i,j) mat.Inds
  if ind == -1
  then ne
  else unsafe(mat.Vals[ind])


let transpose 'a (mat : matrix a) : matrix a =
  let inds = map (\(i,j) -> (j,i)) mat.Inds
  in {Inds = inds, Vals = mat.Vals, Dims = (mat.Dims.2,mat.Dims.1)}

let sparseFlatten 'a (mat : matrix a) : []((i32,i32),a) =
  zip mat.Inds mat.Vals

let sparseMap 'a 'b (mat : matrix a) (fun : a -> b) : matrix b =
  {Inds = mat.Inds, Vals= map fun mat.Vals, Dims=mat.Dims}

let elementwise 'a (mat0 : matrix a) (mat1 : matrix a) (fun : a -> a -> b) (ne : a) =
  if mat0.Dims == mat1.Dims
  then let mat = (zip mat0.Inds mat0.Vals) ++ (zip mat1.Inds mat1.Vals)
       let sort = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if i0==i1 then j0<=j1 else i0 <= i1) mat
       let flags = map2 (\(ind0,_) (ind1,_) -> ind0!=ind1) sort (rotate (-1) sort)
       let (inds,vals) = unzip segmented_reduce (\(_,v0) (i1,v1) -> (i1,fun v0 v1)) ((0,0),ne) flags sort
       in {Inds = inds, Vals = vals, Dims = mat0.Dims}
  else ERROR

let mul 'a (mat0 : matrix a) (mat1 : matrix a) (ne : a) =
  