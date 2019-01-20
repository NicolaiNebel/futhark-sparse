import "lib/github.com/diku-dk/sorts/quick_sort"
import "lib/github.com/diku-dk/segmented/segmented"

import "MonoidEq"
import "csr"

module csc(M : MonoidEq) = {
  type elem = M.t

  type matrix = { dims: (i32, i32)
                , vals: []elem
                , col_ptr: []i32
                , rows: []i32 }

  module dual = csr M

  let fromCsr (m : dual.matrix): matrix =
    -- Grab the dimensions
    let (N,M) = m.dims

    -- Find the lengths of all the rows in m
    let row_lens = map2 (-) (tail m.row_ptr ++ [length m.vals]) m.row_ptr

    -- For each row, expand to (length row) of row index to zip with vals. sum (row_lens) == length m.vals
    let rows: []i32 = expand (.1) (\x _ -> x.2) <| zip row_lens <| iota (length row_lens)
    let tagged_vals: [](elem,i32) = zip m.vals rows

    -- sort the value row pairs by column
    let (sorted_by_col, cols) = zip tagged_vals m.cols |> qsort_by_key (.2) (<=) |> unzip
  
    -- I feel as certain as I can be that this can (and should) be done much, much smarter
    let flags = map2 (!=) cols <| rotate (-1) cols
    let real_cols = zip cols flags |> filter (.2) |> map (.1)
    let col_lens: []i32 = segmented_reduce (+) 0 flags <| replicate (length flags) 1
    let all_col_lens: []i32 = scatter (replicate M 0) real_cols col_lens
    let col_ptr: []i32 = scan (+) 0 <| [0] ++ init all_col_lens

    let (vals, rows) = unzip sorted_by_col

    in { dims = (N,M), vals = vals, col_ptr = col_ptr, rows = rows }
}

module csr_i32 = csr(monoideq_i32)
module csc_i32 = csc(monoideq_i32)

let foo (x : i32) =
  let x = break x
  let m = csr_i32.fromList (5,4) [((1,2),3),((2,0),7),((2,3),4),((3,2),1)]
  let m' = csc_i32.fromCsr m
  in (m.vals, m.row_ptr, m.cols
     ,m'.vals, m'.col_ptr, m'.rows, x)
