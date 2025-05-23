% Generated by roxygen2: do not edit by hand
% Please edit documentation in R/utils.R
\name{H5Mat}
\alias{H5Mat}
\alias{as.H5Mat}
\alias{as.H5Mat.matrix}
\alias{as.H5Mat.dgCMatrix}
\alias{as.H5Mat.default}
\title{Argument list object for using a dense matrix stored in HDF5 file}
\usage{
H5Mat(filename, dataPath)

as.H5Mat(x, filename, dataPath = "data", overwrite = FALSE, ...)

\method{as.H5Mat}{matrix}(x, filename, dataPath = "data", overwrite, ...)

\method{as.H5Mat}{dgCMatrix}(x, filename, dataPath = "data", overwrite = FALSE, ...)

\method{as.H5Mat}{default}(x, filename, dataPath = "data", ...)
}
\arguments{
\item{filename}{Filename of the HDF5 file}

\item{dataPath}{Path in the HDF5 file that points to a 2D dense matrix.
Default \code{"data"} when using \code{as.H5Mat}.}

\item{x}{For \code{as.H5Mat}, matrix of either dense or sparse type to be
written; for \code{print}, a \code{H5Mat} argument list object}

\item{overwrite}{Logical, whether to overwrite the file if already exists at
the given path. Default \code{FALSE}.}

\item{...}{Passed down to hdf5r.Extra::h5Write}
}
\value{
H5Mat object, indeed a list object.
}
\description{
For running \code{\link{inmf}}, \code{\link{onlineINMF}} or
\code{\link{uinmf}} with dense matrix stored in HDF5 file, users will need
to construct an argument list for the filename of the HDF5 file as well as
the path in the file storing the matrix. \code{H5Mat} is provided as an
instructed constructor. Meanwhile, since the INMF functions require that
all datasets should be of the same type, \code{as.H5Mat} is provided for
writing in-memory data into a new HDF5 file on disk and returning the
constructed argument list.
}
\examples{
if (require("withr")) {
H5MatEx <- function(){
withr::local_dir(withr::local_tempdir())
h <- H5Mat(system.file("extdata/ctrl_dense.h5", package = "RcppPlanc"),
           "data")
print(h)

library(Matrix)
ctrl.dense <- as.matrix(ctrl.sparse)
h1 <- as.H5Mat(ctrl.dense, "ctrl_from_dense_to_dense.h5",
               dataPath = "data")
h1
h2 <- as.H5Mat(ctrl.sparse, "ctrl_from_sparse_to_dense.h5",
               dataPath = "data")
}
H5MatEx()
}
}
