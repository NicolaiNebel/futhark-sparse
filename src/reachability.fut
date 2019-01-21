import "lib/github.com/diku-dk/segmented/segmented"
import "tupleSparse"
import "MonoidEq"

module boolean_monoid = {
  type t = bool

  let add: (bool -> bool -> bool) = (||)
  let eq: (bool -> bool -> bool) = (==)
  let mul: (bool -> bool -> bool) = (&&)
  let zero: bool = false
}

module coo_boolean = spCoord(boolean_monoid)

let floyd_warshall (m : coo_boolean.matrix) : coo_boolean.matrix =
  let (N,M) = m.Dims
  in if N == M
  then
    loop result = m for k < N do
      -- Rows to update
      let update_rows = map (.1) <| filter (\(_,x) -> x == k) result.Inds
      let row_cols = map (.2) <| filter (\(x,_) -> x == k) result.Inds
      let sz   = \_ -> length row_cols
      let get  = \x i -> (x, unsafe(row_cols[i]))
      let inds = expand sz get update_row
      let m' = { Inds = inds, Vals = map (\_ -> true) inds, Dims = result.Dims }
      in coo_boolean.elementwise result m' boolean_monoid.add boolean_monoid.zero
  else
    coo_boolean.empty N M
