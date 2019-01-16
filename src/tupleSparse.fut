import "lib/github.com/diku-dk/sorts/merge_sort"
import "lib/github.com/diku-dk/segmented/segmented"
import "futlib/math"

module coord = {
  --type mul = i8,i16,i32,i64,u8,u16,u32,u64,f32,f64
  type matrix 'a = { Inds : [](i32,i32), Vals : []a, Dims : (i32,i32) }

let fromList 'a (dim : (i32,i32)) (l : []((i32,i32),a)) : matrix a =
  let (inds,vals) = unzip l
  in {Inds = inds, Vals = vals, Dims = dim}

--General solution
let fromDense 'a [m][n] (ne : a) (eq : a -> a -> bool) (dense : [m][n]a) =
  let inds = flatten <| map (\i -> map (\j -> (i,j)) (iota n)) <| iota m
  let denseInds = zip inds <| flatten dense
  let listVals = filter (\(_,v) -> ! (eq v ne)) denseInds
  in fromList (m,n) listVals


let toDense 'a (ne : a) (mat : matrix a) =
  let dim = mat.Dims
  let dense = replicate (dim.1*dim.2) ne
  let inds = map (\(i,j) -> i+j*dim.1) mat.Inds
  let res = scatter dense inds mat.Vals
  in unflatten dim.1 dim.2 res

let empty 'a (dim : (i32,i32)) : matrix a= {Inds = [], Vals=[], Dims=dim}


let diag 'a (size :i32) (el : a) : matrix a =
  let (inds, vals) = unzip <| map (\i -> ((i,i),el)) (iota size)
  in {Inds = inds, Vals = vals, Dims= (size,size)}


let find_idx_first 'v [n] (e:v)  (eq : v -> v -> bool) (xs:[n]v) : i32 =
  let es = map2 (\x i -> if eq x e then i else n) xs (iota n)
  let res = reduce i32.min n es
  in if res == n then -1 else res

let update 'a (mat : matrix a) i j (el : a) : matrix a =
  if i>=mat.Dims.1 && j>=mat.Dims.2
  then mat
  else let ind = find_idx_first (i,j) (==) mat.Inds in
       if ind != -1
       then let vals = copy mat.Vals
            in {Inds = mat.Inds, Vals = scatter vals [ind] [el], Dims=mat.Dims}
       else {Inds = mat.Inds++[(i,j)], Vals = mat.Vals++[el], Dims=mat.Dims}

let get 'a (mat : matrix a) (ne : a) i j : a =
  let ind = find_idx_first (i,j) (==) mat.Inds
  in if ind == (-1)
     then ne
     else unsafe(mat.Vals[ind])


let transpose 'a (mat : matrix a) : matrix a =
  let inds = map (\(i,j) -> (j,i)) mat.Inds
  in {Inds = inds, Vals = mat.Vals, Dims = (mat.Dims.2,mat.Dims.1)}

let sparseFlatten 'a (mat : matrix a) : []((i32,i32),a) =
  zip mat.Inds mat.Vals

let sparseMap 'a 'b (mat : matrix a) (fun : a -> b) : matrix b =
  {Inds = mat.Inds, Vals= map fun mat.Vals, Dims=mat.Dims}

let elementwise 'a (mat0 : matrix a) (mat1 : matrix a) (fun : a -> a -> a) (ne : a) : matrix a =
  if mat0.Dims == mat1.Dims
  then let mat = (zip mat0.Inds mat0.Vals) ++ (zip mat1.Inds mat1.Vals)
       let sort = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if i0==i1 then j0<=j1 else i0 <= i1) mat
       let flags = map2 (\(ind0,_) (ind1,_) -> ind0!=ind1) sort (rotate (-1) sort)
       let (inds,vals) = unzip <| segmented_reduce (\(_,v0) (i1,v1) -> (i1,fun v0 v1)) ((0,0),ne) flags sort
       in {Inds = inds, Vals = vals, Dims = mat0.Dims}
  else mat0


let matMult 'a (sort0 : []((i32,i32),a)) count0 (sort1 : []((i32,i32),a)) count1 (ne : a) (mul : a->a->a) (plus : a->a->a) i j =
  let part0 = sort0[count0[i]:count0[i+1]]
  let part1 = sort1[count1[j]:count1[j+1]]
  let is = map (\(ind,_) -> ind.1) part1
  let res = map (\((_,j),v) -> let ind = find_idx_first j (==) is
                               in if ind == -1 then ne
                                  else mul v (unsafe( part1[ind] ).2)
                ) part0
  in ((i,j),reduce plus ne res)

let mul 'a (mat0 : matrix a) (mat1 : matrix a) (ne : a) (mul : a -> a -> a) (plus : a->a->a) (eq : a->a->bool) : matrix a =
  if mat0.Dims.2 == mat1.Dims.1
  then let sort0 = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if i0==i1 then j0<=j1 else i0 <= i1) (zip mat0.Inds mat0.Vals)
       let sort1 = merge_sort (\((i0,j0),_) ((i1,j1),_)-> if j0==j1 then i0<=i1 else j0 <= j1) (zip mat1.Inds mat1.Vals)
       let flag0 = map2 (\((ind0,_),_) ((ind1,_),_) -> ind0!=ind1) sort0 (rotate (-1) sort0)
       let flag0i = map (\b -> if b then 1 else 0) flag0
       let count0 = (++) [0] <| segmented_reduce (\i _-> i+1) 0 flag0 flag0i
       let flag1 = map2 (\((_,ind0),_) ((_,ind1),_) -> ind0!=ind1) sort1 (rotate (-1) sort1)
       let flag1i = map (\b -> if b then 1 else 0) flag1
       let count1 =(++)  [0] <| segmented_reduce (\i _-> i+1) 0 flag1 flag1i
       let dense = expand (\_ -> mat1.Dims.2) (matMult sort0 count0 sort1 count1 ne mul plus) (iota mat0.Dims.1)
       let (inds,vals) =unzip <| filter (\(_,v) -> ! (eq v ne)) dense
       in {Inds = inds, Vals = vals, Dims = (mat0.Dims.1,mat1.Dims.2)}
  else mat0
}