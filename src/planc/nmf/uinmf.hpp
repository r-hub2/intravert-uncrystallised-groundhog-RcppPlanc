#pragma once

#ifdef _OPENMP
#include <omp.h>
#endif
#include "bppnnls.hpp"
#include "inmf.hpp"

namespace planc {
    template<typename T>
    class UINMF : public INMF<T> {
    private:
        arma::mat giventGiven;
        std::vector<std::shared_ptr<T>> Pi;
        std::vector<std::shared_ptr<T>> PiT;
        std::vector<std::unique_ptr<arma::mat>> Ui;
        arma::uvec u; // Number of unshared features
        arma::vec lambda_i;
        bool uinmf_cleared = false;
        std::vector<int> whichUnshared; // SIGNED, indicator for each dataset,
        // which index in Pi/Ui is the unshared part,
        // -1 for no unshared feature

        void sampleUandV() {
            // U and V must be sampled from the same random set of cells, thus put together
#ifdef _VERBOSE
#ifdef USING_R
            Rcpp::Rcout
#else
            std::cout
#endif
                << "Initializing U and V matrices by sampling from input" << std::endl;
#endif
            std::unique_ptr<arma::mat> U;
            std::unique_ptr<arma::mat> V;
            for (arma::uword i = 0; i < this->nDatasets; ++i) {
                arma::uvec indices = arma::randperm(this->ncol_E[i]).head(this->k);
                V = std::make_unique<arma::mat>(this->m, this->k, arma::fill::zeros);
                arma::mat* Vptr = V.get();
                *Vptr = this->Ei[i].get()->cols(indices);
                this->Vi.push_back(std::move(V));
                int uidx = this->whichUnshared[i];
                if (uidx == -1) continue; // skip if no U
                U = std::make_unique<arma::mat>(this->u[uidx], this->k, arma::fill::zeros);
                arma::mat* Uptr = U.get();
                *Uptr = this->Pi[uidx].get()->cols(indices);
                this->Ui.push_back(std::move(U));
            }
        }

        void initW2() {
            // Initialization is different than regular iNMF, so "2"
            // NOTE that this is also different from the initW2 in onlineINMF
            this->W = std::make_unique<arma::mat>();
            arma::mat* Wptr = this->W.get();
            *Wptr = arma::randu<arma::mat>(this->m, this->k, arma::distr_param(0, 2));
        }

        void initH() override {
#ifdef _VERBOSE
#ifdef USING_R
        Rcpp::Rcout
#else
        std::cout
#endif
        << "Randomly initializing H matrices" << std::endl;
#endif
            std::unique_ptr<arma::mat> H;
            for (arma::uword i = 0; i < this->nDatasets; ++i) {
                H = std::make_unique<arma::mat>();
                arma::mat* Hptr = H.get();
                *Hptr = arma::randu<arma::mat>(this->ncol_E[i], this->k, arma::distr_param(0, 2));
                this->Hi.push_back(std::move(H));
            }
        }

        double computeObjectiveError() override {
            double obj = 0;
            arma::mat* Wptr = this->W.get();
            arma::mat L(this->m, this->k);
            for (arma::uword i = 0; i < this->nDatasets; ++i) {
                T* Eptr = this->Ei[i].get();
                arma::mat* Hptr = this->Hi[i].get();
                arma::mat* Vptr = this->Vi[i].get();
                double sqnormE = arma::norm<T>(*Eptr, "fro");
                sqnormE *= sqnormE;
                L = *Wptr + *Vptr;
                arma::mat LtL = L.t() * L;
                arma::mat HtH = Hptr->t() * *Hptr;
                arma::mat VtV = Vptr->t() * *Vptr;
                arma::mat EtL = Eptr->t() * L;
                double TrLtLHtH = arma::trace(LtL * HtH);
                double TrHtEtL = arma::trace(Hptr->t() * EtL);
                double TrVtVHtH = arma::trace(VtV * HtH);
                obj += sqnormE + TrLtLHtH - 2 * TrHtEtL + this->lambda_i[i] * TrVtVHtH;
                int uidx = this->whichUnshared[i];
                if (uidx >= 0) {
                    T* Pptr = this->Pi[uidx].get();
                    arma::mat* Uptr = this->Ui[uidx].get();
                    double sqnormP = arma::norm<T>(*Pptr, "fro");
                    sqnormP *= sqnormP;
                    arma::mat UtU = Uptr->t() * *Uptr;
                    arma::mat PtU = Pptr->t() * *Uptr;
                    double TrUtUHtH = arma::trace(UtU * HtH);
                    double TrHtPtU = arma::trace(Hptr->t() * PtU);
                    obj += sqnormP + (1 + this->lambda_i[i]) * TrUtUHtH - 2 * TrHtPtU;
                }
            }
            return obj;
        }

        void solveH(int ncores) {
#ifdef _VERBOSE
#ifdef USING_R
        Rcpp::Rcout
#else
        std::cout
#endif
        << "--Solving UINMF H--  ";
        std::chrono::system_clock::time_point iter_start_time = std::chrono::system_clock::now();
#endif
            arma::mat* Wptr = this->W.get();
            for (arma::uword i = 0; i < this->nDatasets; ++i) {
                arma::mat given(this->m, this->k);
                arma::mat* Vptr = this->Vi[i].get();
                arma::mat* Hptr = this->Hi[i].get();
                T* Eptr = this->Ei[i].get();
                arma::mat WV = *Wptr + *Vptr;
                giventGiven = WV.t() * WV;
                giventGiven += Vptr->t() * *Vptr * this->lambda_i[i];
                int uidx = this->whichUnshared[i];
                if (uidx >= 0) {
                    arma::mat* Uptr = this->Ui[uidx].get();
                    giventGiven += Uptr->t() * (*Uptr) * (1 + this->lambda_i[i]);
                }
                int dataSize = this->ncol_E[i];
                int numChunks = dataSize / this->INMF_CHUNK_SIZE;
                if (numChunks * this->INMF_CHUNK_SIZE < dataSize) numChunks++;
#pragma omp parallel for schedule(dynamic) default(none) shared(Eptr, i, uidx, WV, Hptr, numChunks, dataSize) num_threads(ncores)
                for (int j = 0; j < numChunks; ++j) {
                    int spanStart = j * this->INMF_CHUNK_SIZE;
                    int spanEnd = (j + 1) * this->INMF_CHUNK_SIZE - 1;
                    if (spanEnd > dataSize - 1) spanEnd = dataSize - 1;
                    arma::mat giventInput = WV.t() * (*Eptr).cols(spanStart, spanEnd);
                    if (uidx >= 0) {
                        T* Pptr = this->Pi[uidx].get();
                        arma::mat* Uptr = this->Ui[uidx].get();
                        giventInput += Uptr->t() * (*Pptr).cols(spanStart, spanEnd);
                    }
                    BPPNNLS<arma::mat, arma::vec> subProbH(giventGiven, giventInput, true);
                    subProbH.solveNNLS();
                    Hptr->rows(spanStart, spanEnd) = subProbH.getSolutionMatrix().t();
                    giventInput.clear();
                }
            }
            giventGiven.clear();
#ifdef _VERBOSE
        std::chrono::system_clock::time_point iter_end_time = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = iter_end_time - iter_start_time;
#ifdef USING_R
        Rcpp::Rcout
#else
        std::cout
#endif
        << "Solving H took " << elapsed_seconds.count() << " sec" << std::endl;
#endif
        }


        void solveV(int ncores) {
#ifdef _VERBOSE
#ifdef USING_R
        Rcpp::Rcout
#else
        std::cout
#endif
        << "--Solving UINMF V--  ";
        std::chrono::system_clock::time_point iter_start_time = std::chrono::system_clock::now();
#endif
            arma::mat* Wptr = this->W.get();
            arma::mat giventInput(this->k, this->INMF_CHUNK_SIZE);
            for (arma::uword i = 0; i < this->nDatasets; ++i) {
                arma::mat* Hptr = this->Hi[i].get();\
                giventGiven = Hptr->t() * (*Hptr);
                giventGiven *= 1 + this->lambda_i[i];
                arma::mat* Vptr = this->Vi[i].get();
                T* ETptr = this->EiT[i].get();
                int numChunks = this->m / this->INMF_CHUNK_SIZE;
                if (numChunks * this->INMF_CHUNK_SIZE < this->m) numChunks++;
#pragma omp parallel for schedule(dynamic) default(none) shared(i, Hptr, numChunks, Wptr, Vptr, ETptr) num_threads(ncores)
                for (int j = 0; j < numChunks; ++j) {
                    int spanStart = j * this->INMF_CHUNK_SIZE;
                    int spanEnd = (j + 1) * this->INMF_CHUNK_SIZE - 1;
                    if (spanEnd > this->m - 1) spanEnd = this->m - 1;
                    arma::mat giventInputTLS = Hptr->t() * ETptr->cols(spanStart, spanEnd);
                    giventInputTLS -= Hptr->t() * *Hptr * Wptr->rows(spanStart, spanEnd).t();
                    BPPNNLS<arma::mat, arma::vec> subProbV(giventGiven, giventInputTLS, true);
                    subProbV.solveNNLS();
                    Vptr->rows(spanStart, spanEnd) = subProbV.getSolutionMatrix().t();
                }
            }
            giventGiven.clear();
            giventInput.clear();
#ifdef _VERBOSE
        std::chrono::system_clock::time_point iter_end_time = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = iter_end_time - iter_start_time;
#ifdef USING_R
        Rcpp::Rcout
#else
        std::cout
#endif
        << "Solving V took " << elapsed_seconds.count() << " sec" << std::endl;
#endif
        }

        void solveU(int ncores) {
#ifdef _VERBOSE
#ifdef USING_R
        Rcpp::Rcout
#else
        std::cout
#endif
        << "--Solving UINMF U--  ";
        std::chrono::system_clock::time_point iter_start_time = std::chrono::system_clock::now();
#endif
            arma::mat giventInput(this->k, this->INMF_CHUNK_SIZE);
            for (arma::uword i = 0; i < this->nDatasets; ++i) {
                int uidx = this->whichUnshared[i];
                if (uidx == -1) continue; // skip if no U
                arma::mat* Hptr = this->Hi[i].get();
                arma::mat* Uptr = this->Ui[uidx].get();
                giventGiven = Hptr->t() * (*Hptr);
                giventGiven *= 1 + this->lambda_i[i];
                // T* Pptr = this->Pi[uidx].get();
                T* PTptr = this->PiT[uidx].get();
                int numChunks = this->u[uidx] / this->INMF_CHUNK_SIZE;
                if (numChunks * this->INMF_CHUNK_SIZE < this->u[uidx]) numChunks++;
#pragma omp parallel for schedule(dynamic) default(none) shared(i, Uptr, Hptr, numChunks, PTptr, uidx) num_threads(ncores)
                for (int j = 0; j < numChunks; ++j) {
                    int spanStart = j * this->INMF_CHUNK_SIZE;
                    int spanEnd = (j + 1) * this->INMF_CHUNK_SIZE - 1;
                    if (spanEnd > this->u[uidx] - 1) spanEnd = this->u[uidx] - 1;
                    arma::mat giventInputTLS = Hptr->t() * PTptr->cols(spanStart, spanEnd);
                    BPPNNLS<arma::mat, arma::vec> subProbU(giventGiven, giventInputTLS, true);
                    subProbU.solveNNLS();
                    Uptr->rows(spanStart, spanEnd) = subProbU.getSolutionMatrix().t();
                }
            }
            giventGiven.clear();
            giventInput.clear();
#ifdef _VERBOSE
        std::chrono::system_clock::time_point iter_end_time = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = iter_end_time - iter_start_time;
#ifdef USING_R
        Rcpp::Rcout
#else
        std::cout
#endif
        << "Solving U took " << elapsed_seconds.count() << " sec" << std::endl;
#endif
        }

        void solveW(int ncores) {
#ifdef _VERBOSE
#ifdef USING_R
        Rcpp::Rcout
#else
        std::cout
#endif
        << "--Solving UINMF W--  ";
        std::chrono::system_clock::time_point iter_start_time = std::chrono::system_clock::now();
#endif
            arma::mat* Wptr = this->W.get();
            giventGiven = arma::zeros<arma::mat>(this->k, this->k); ///
            arma::mat giventInput;
            for (unsigned int j = 0; j < this->nDatasets; ++j) {
                arma::mat* Hptr = this->Hi[j].get();
                giventGiven += Hptr->t() * *Hptr;
            }

            int numChunks = this->m / this->INMF_CHUNK_SIZE;
            if (numChunks * this->INMF_CHUNK_SIZE < this->m) numChunks++;
            for (int i = 0; i < numChunks; ++i) {
                int spanStart = i * this->INMF_CHUNK_SIZE;
                int spanEnd = (i + 1) * this->INMF_CHUNK_SIZE - 1;
                if (spanEnd > this->m - 1) spanEnd = this->m - 1;
                giventInput = arma::zeros<arma::mat>(this->k, spanEnd - spanStart + 1); ///
#pragma omp parallel for ordered schedule(dynamic) default(none) shared(i, numChunks, spanStart, spanEnd, giventInput) num_threads(ncores)
                for (int j = 0; j < this->nDatasets; ++j) {
                    T* ETptr = this->EiT[j].get();
                    arma::mat* Hptr = this->Hi[j].get();
                    // arma::mat* VTptr = this->ViT[j].get();
                    arma::mat* Vptr = this->Vi[j].get();
                    arma::mat gtIpos = Hptr->t() * ETptr->cols(spanStart, spanEnd);
                    arma::mat gtIneg = Hptr->t() * *Hptr * Vptr->rows(spanStart, spanEnd).t();
#pragma omp ordered
                    {
                        giventInput += gtIpos;
                        giventInput -= gtIneg;
                    }
                }
                BPPNNLS<arma::mat, arma::vec> subProbW(giventGiven, giventInput, true); ///
                subProbW.solveNNLS();
                Wptr->rows(spanStart, spanEnd) = subProbW.getSolutionMatrix().t();
                // (*WTptr).cols(spanStart, spanEnd) = subProbW.getSolutionMatrix();
            }
            giventGiven.clear(); ///
            giventInput.clear(); ///
#ifdef _VERBOSE
        std::chrono::system_clock::time_point iter_end_time = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = iter_end_time - iter_start_time;
#ifdef USING_R
            Rcpp::Rcout
#else
            std::cout
#endif
        << "Solving W took " << elapsed_seconds.count() << " sec" << std::endl;
#endif
        }

    public:
        UINMF(const std::vector<std::shared_ptr<T>>&Ei,
              const std::vector<std::shared_ptr<T>>&Pi,
              std::vector<int>&whichUnshared,
              arma::uword k, const arma::vec&lambda) : INMF<T>(Ei, k, 0, true) {
            this->Vi.clear();
            this->ViT.clear();
            this->Hi.clear();
            this->W.reset();
            this->WT.reset();
            this->Pi = std::move(Pi);
            this->lambda_i = lambda;
            this->whichUnshared = whichUnshared;
            u = arma::zeros<arma::uvec>(this->Pi.size());
            for (arma::uword i = 0; i < this->Pi.size(); ++i) {
                u[i] = this->Pi[i]->n_rows;
                T* Pptr = this->Pi[i].get();
                T PT = Pptr->t();
                std::shared_ptr<T> PTptr = std::make_shared<T>(PT);
                this->PiT.push_back(std::move(PTptr));
            }
        }

        void optimizeUANLS(arma::uword niter = 30, bool verbose = true, const int&ncores = 0) {
            if (verbose) {
#ifdef USING_R
                Rcpp::Rcerr
#else
            std::cerr
#endif
                        << "UINMF started, niter=" << niter << std::endl;
            }
            const auto start = std::chrono::high_resolution_clock::now();
            this->sampleUandV();
            this->initW2();
            this->initH();
            Progress p(niter, verbose);
            for (unsigned int iter = 0; iter < niter; iter++) {
#ifdef USING_R
                Rcpp::checkUserInterrupt();
#endif
                this->solveH(ncores);
                this->solveV(ncores);
                this->solveU(ncores);
                this->solveW(ncores);
#ifdef USING_R
                if (!p.is_aborted())
#endif
                    p.increment();
#ifdef USING_R
                else break;
#endif
            }
            this->objective_err = this->computeObjectiveError();
            const auto end = std::chrono::high_resolution_clock::now();
            const auto duration = std::chrono::duration_cast<std::chrono::seconds>(end - start);
            if (verbose) {
#ifdef USING_R
                Rcpp::Rcerr
#else
            std::cerr
#endif
                        << "Total time:      " << duration.count() << " sec" << std::endl;
#ifdef USING_R
                Rcpp::Rcerr
#else
            std::cerr
#endif
                        << "Objective error: " << this->objective_err << std::endl;
            }
        }

        arma::mat getUi(int uidx) {
            return *(this->Ui[uidx]);
        }

        std::vector<std::unique_ptr<arma::mat>> getAllU() {
            return std::move(this->Ui);
        }

        ~UINMF() override {
            if (!this->uinmf_cleared) {
                for (unsigned int i = 0; i < this->Ui.size(); ++i) {
                    this->Ui[i].reset();
                    this->Pi[i].reset();
                }
                this->cleared = true;
            }
        }
    }; // class UINMF

    // template<>
    // inline double UINMF<H5Mat>::computeObjectiveError() {
    //     double obj = 0;
    //     arma::mat* Wptr = this->W.get();
    //     arma::mat L(this->m, this->k);
    //     for (arma::uword i = 0; i < this->nDatasets; ++i) {
    //         H5Mat* Eptr = this->Ei[i].get();
    //         arma::mat* Hptr = this->Hi[i].get();
    //         arma::mat* Vptr = this->Vi[i].get();
    //         double sqnormE = Eptr->normF();
    //         sqnormE *= sqnormE;
    //         L = *Wptr + *Vptr;
    //         arma::mat LtL = L.t() * L;
    //         arma::mat HtH = Hptr->t() * *Hptr;
    //         arma::mat VtV = Vptr->t() * *Vptr;
    //         arma::mat EtL(this->ncol_E[i], this->k);
    //         arma::uword numChunks = Eptr->n_cols / Eptr->colChunkSize;
    //         if (numChunks * Eptr->colChunkSize < Eptr->n_cols) numChunks++;
    //         for (arma::uword j = 0; j < numChunks; ++j) {
    //             arma::uword spanStart = j * Eptr->colChunkSize;
    //             arma::uword spanEnd = (j + 1) * Eptr->colChunkSize - 1;
    //             if (spanEnd > Eptr->n_cols - 1) spanEnd = Eptr->n_cols - 1;
    //             EtL.rows(spanStart, spanEnd) = Eptr->cols(spanStart, spanEnd).t() * L;
    //         }
    //         double TrLtLHtH = arma::trace(LtL * HtH);
    //         double TrHtEtL = arma::trace(Hptr->t() * EtL);
    //         double TrVtVHtH = arma::trace(VtV * HtH);
    //         obj += sqnormE + TrLtLHtH - 2 * TrHtEtL  + this->lambda_i[i] * TrVtVHtH;
    //
    //         int uidx = this->whichUnshared[i];
    //         if (uidx == -1) continue; // skip if no U
    //         H5Mat* Pptr = this->Pi[uidx].get();
    //         arma::mat* Uptr = this->Ui[uidx].get();
    //         double sqnormP = Pptr->normF();
    //         sqnormP *= sqnormP;
    //         arma::mat UtU = Uptr->t() * *Uptr;
    //         arma::mat PtU(this->ncol_E[i], this->k);
    //         numChunks = Pptr->n_cols / Pptr->colChunkSize;
    //         if (numChunks * Pptr->colChunkSize < Pptr->n_cols) numChunks++;
    //         for (arma::uword j = 0; j < numChunks; ++j) {
    //             arma::uword spanStart = j * Pptr->colChunkSize;
    //             arma::uword spanEnd = (j + 1) * Pptr->colChunkSize - 1;
    //             if (spanEnd > Pptr->n_cols - 1) spanEnd = Pptr->n_cols - 1;
    //             PtU.rows(spanStart, spanEnd) = Pptr->cols(spanStart, spanEnd).t() * *Uptr;
    //         }
    //         double TrUtUHtH = arma::trace(UtU * HtH);
    //         double TrHtUstU = arma::trace(Hptr->t() * PtU);
    //         obj += sqnormP + (1+this->lambda_i[i]) * TrUtUHtH - 2 * TrHtUstU;
    //     }
    //     return obj;
    // }
    //
    // template<>
    // inline double UINMF<H5SpMat>::computeObjectiveError() {
    //     double obj = 0;
    //     arma::mat* Wptr = this->W.get();
    //     arma::mat L(this->m, this->k);
    //     for (arma::uword i = 0; i < this->nDatasets; ++i) {
    //         H5SpMat* Eptr = this->Ei[i].get();
    //
    //         arma::mat* Hptr = this->Hi[i].get();
    //         arma::mat* Vptr = this->Vi[i].get();
    //
    //         double sqnormE = Eptr->normF();
    //         sqnormE *= sqnormE;
    //
    //         L = *Wptr + *Vptr;
    //         arma::mat LtL = L.t() * L;
    //         arma::mat HtH = Hptr->t() * *Hptr;
    //         arma::mat VtV = Vptr->t() * *Vptr;
    //
    //         arma::mat EtL(this->ncol_E[i], this->k);
    //         arma::uword numChunks = Eptr->n_cols / this->INMF_CHUNK_SIZE;
    //         if (numChunks * this->INMF_CHUNK_SIZE < Eptr->n_cols) numChunks++;
    //         for (arma::uword j = 0; j < numChunks; ++j) {
    //             arma::uword spanStart = j * this->INMF_CHUNK_SIZE;
    //             arma::uword spanEnd = (j + 1) * this->INMF_CHUNK_SIZE - 1;
    //             if (spanEnd > Eptr->n_cols - 1) spanEnd = Eptr->n_cols - 1;
    //             EtL.rows(spanStart, spanEnd) = Eptr->cols(spanStart, spanEnd).t() * L;
    //         }
    //         double TrLtLHtH = arma::trace(LtL * HtH);
    //         double TrHtEtL = arma::trace(Hptr->t() * EtL);
    //         double TrVtVHtH = arma::trace(VtV * HtH);
    //         obj += sqnormE + TrLtLHtH - 2 * TrHtEtL + this->lambda_i[i] * TrVtVHtH;
    //
    //         int uidx = this->whichUnshared[i];
    //         if (uidx == -1) continue; // skip if no U
    //         H5SpMat* Pptr = this->Pi[uidx].get();
    //         arma::mat* Uptr = this->Ui[uidx].get();
    //         double sqnormP = Pptr->normF();
    //         sqnormP *= sqnormP;
    //         arma::mat UtU = Uptr->t() * *Uptr;
    //         arma::mat PtU(this->ncol_E[i], this->k);
    //         // arma::uword numChunks = Pptr->n_cols / this->INMF_CHUNK_SIZE;
    //         if (numChunks * this->INMF_CHUNK_SIZE < Eptr->n_cols) numChunks++;
    //         for (arma::uword j = 0; j < numChunks; ++j) {
    //             arma::uword spanStart = j * this->INMF_CHUNK_SIZE;
    //             arma::uword spanEnd = (j + 1) * this->INMF_CHUNK_SIZE - 1;
    //             if (spanEnd > Eptr->n_cols - 1) spanEnd = Eptr->n_cols - 1;
    //             PtU.rows(spanStart, spanEnd) = Pptr->cols(spanStart, spanEnd).t() * *Uptr;
    //         }
    //         double TrUtUHtH = arma::trace(UtU * HtH);
    //         double TrHtPtU = arma::trace(Hptr->t() * PtU);
    //         obj += sqnormP + (1+this->lambda_i[i]) * TrUtUHtH - 2 * TrHtPtU;
    //     }
    //     return obj;
    //}
} // namespace planc
