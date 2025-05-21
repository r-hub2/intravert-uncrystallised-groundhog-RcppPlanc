#pragma once
/* Copyright 2016 Ramakrishnan Kannan */

#ifdef _OPENMP
#include <omp.h>
#endif
#include "config.h"
#include "bppnnls.hpp"
#include "hals.hpp"

namespace planc {
    template<typename T>
    class BPPNMF : public NMF<T> {
    private:
        T At;
        //arma::mat giventGiven;
        arma::uword ONE_THREAD_MATRIX_SIZE; // chunking
        // designed as if W is given and H is found.
        // The transpose is the other problem.
        void updateOtherGivenOneMultipleRHS(const T&input, const arma::mat&given,
                                            char worh, arma::mat* othermat, arma::fvec reg) {
#if defined(_VERBOSE) || defined(COLLECTSTATS)
    double t2;
#endif
            this->ONE_THREAD_MATRIX_SIZE = chunk_size_dense<typename T::elem_type>(this->W.n_cols);
            int numChunks = input.n_cols / this->ONE_THREAD_MATRIX_SIZE;
            if (numChunks * this->ONE_THREAD_MATRIX_SIZE < input.n_cols) numChunks++;
#if defined(_VERBOSE) || defined(COLLECTSTATS)
    tic();
#endif
            arma::mat giventInput(this->k, input.n_cols);
            // This is WtW
            arma::mat giventGiven = given.t() * given;
            this->applyReg(reg, &giventGiven);
            // This is WtA
            giventInput = given.t() * input;
            if (this->symm_reg() > 0) {
                arma::mat fac = given.t();
                this->applySymmetricReg(this->symm_reg(), &giventGiven, &fac,
                                        &giventInput);
            }
#if defined(_VERBOSE) || defined(COLLECTSTATS)
    t2 = toc();
#endif
#ifdef _VERBOSE
    INFO << "starting " << worh << ". Prereq for " << worh << " took=" << t2
         << " NumChunks=" << numChunks << std::endl;
    INFO << "LHS::" << std::endl
         << giventGiven << std::endl
         << "RHS::" << std::endl
         << giventInput << std::endl;
#endif
#if defined(_VERBOSE) || defined(COLLECTSTATS)
    tic();
#endif
            std::vector<std::unique_ptr<BPPNNLS<arma::mat, arma::vec>>> subproblems;
            std::vector<std::pair<int, int>> indices;
#pragma omp parallel default(none) shared(othermat, input, giventInput, giventGiven, subproblems, indices, numChunks) num_threads(this->ncores)
            {
#pragma omp for schedule(dynamic)
                for (int i = 0; i < numChunks; i++) {
                    int spanStart = i * this->ONE_THREAD_MATRIX_SIZE;
                    int spanEnd = (i + 1) * this->ONE_THREAD_MATRIX_SIZE - 1;
                    if (spanEnd > input.n_cols - 1) {
                        spanEnd = input.n_cols - 1;
                    }
                    std::unique_ptr<BPPNNLS<arma::mat, arma::vec>> subProblem(new BPPNNLS<arma::mat, arma::vec>(
                        giventGiven,
                        static_cast<arma::mat>(giventInput.cols(spanStart, spanEnd)), true));

#pragma omp critical
                    {
                        subproblems.push_back(std::move(subProblem));
                        indices.emplace_back(spanStart, spanEnd);
                    }
#ifdef _VERBOSE
#pragma omp critical
              {
                INFO << "Scheduling " << worh << " start=" << spanStart
                     << ", end=" << spanEnd
                     // << ", tid=" << omp_get_thread_num()
                     << std::endl
                     << "LHS ::" << std::endl
                     << giventGiven << std::endl
                     << "RHS ::" << std::endl
                     << (arma::mat)giventInput.cols(spanStart, spanEnd) << std::endl;
              }
#endif
                }
#pragma omp for schedule(dynamic)
                for (int i = 0; i < subproblems.size(); i++) {
                    subproblems[i]->solveNNLS();



#ifdef _VERBOSE
              INFO << "completed " << worh << " start=" << spanStart
                   << ", end=" << spanEnd
                   // << ", tid=" << omp_get_thread_num() << " cpu=" << sched_getcpu()
                   << " time taken=" << t2 << std::endl;
#endif
                }
            }
#pragma omp for schedule(dynamic)
            for (int i = 0; i < subproblems.size(); i++) {
                othermat->rows(indices[i].first, indices[i].second) = subproblems[i]->getSolutionMatrix().t();
            }

#if defined(_VERBOSE) || defined(COLLECTSTATS)
          double totalH2 = toc();
#endif
#ifdef _VERBOSE
          INFO << worh << " total time taken :" << totalH2 << std::endl;
#endif
            giventGiven.clear();
            giventInput.clear();
        }

        void commonSolve() {
            unsigned int currentIteration = 0;
            while (currentIteration < this->num_iterations()) {
#ifdef COLLECTSTATS
      this->collectStats(currentIteration);
      this->stats(currentIteration + 1, 0) = currentIteration + 1;
#endif
#if defined(_VERBOSE) || defined(COLLECTSTATS)
      tic();
#endif
                updateOtherGivenOneMultipleRHS(this->At, this->H, 'W', &(this->W),
                                               this->regW());
#if defined(_VERBOSE) || defined(COLLECTSTATS)
      double totalW2 = toc();
      tic();
#endif
                updateOtherGivenOneMultipleRHS(this->A, this->W, 'H', &(this->H),
                                               this->regH());
#if defined(_VERBOSE) || defined(COLLECTSTATS)
      double totalH2 = toc();
#endif

#ifdef COLLECTSTATS
      // end of H and start of W are almost same.
      this->stats(currentIteration + 1, 1) = totalH2;
      this->stats(currentIteration + 1, 2) = totalW2;

      this->stats(currentIteration + 1, 3) = totalW2 + totalH2;
#endif
#ifdef _VERBOSE
      INFO << "Completed It (" << currentIteration << "/"
           << this->num_iterations() << ")"
           << " time =" << totalW2 + totalH2 << std::endl;
      this->computeObjectiveError();
      this->printObjective(currentIteration);

#endif
                currentIteration++;
            }
            this->normalize_by_W();
            this->computeObjectiveError();
#ifdef COLLECTSTATS
    this->collectStats(currentIteration);
    INFO << "NMF Statistics:" << std::endl << this->stats << std::endl;
#endif
        };

    public:
        BPPNMF(const T&A, int lowrank, const int&ncores = 0) : NMF<T>(A, lowrank) {
            arma::mat giventGiven = arma::zeros<arma::mat>(lowrank, lowrank);
            this->At = A.t();
            this->ncores = ncores;
        }

        BPPNMF(const T&A, const arma::mat&llf, const arma::mat&rlf, const int&ncores = 0) : NMF<T>(A, llf, rlf) {
            this->At = A.t();
            this->ncores = ncores;
        }

        void computeNMFSingleRHS() {
            int currentIteration = 0;
            this->At = this->A.t();
            this->computeObjectiveErr();
            while (currentIteration < this->num_iterations() &&
                   this->objectiveErr > CONV_ERR) {
#ifdef COLLECTSTATS
      this->collectStats(currentIteration);
#endif
                // solve for H given W;
                arma::mat Wt = this->W.t();
                arma::mat WtW = Wt * this->W;
                this->applyReg(this->regH(), &this->WtW);
                arma::mat WtA = Wt * this->A;
                Wt.clear(); {
#pragma omp parallel for schedule(dynamic) default(none) num_threads(this->ncores)
                    for (int i = 0; i < this->n; i++) {
                        auto subProblemforH =
                                BPPNNLS<arma::mat, arma::vec>(WtW, static_cast<arma::vec>(WtA.col(i)), true);
#ifdef _VERBOSE
          INFO << "Initialized subproblem and calling solveNNLS for "
               << "H(" << i << "/" << this->n << ")";
#endif
#if defined(_VERBOSE) || defined(COLLECTSTATS)
          tic();
          int numIter = subProblemforH->solveNNLS();
#else
                        subProblemforH.solveNNLS();
#endif

#if defined(_VERBOSE) || defined(COLLECTSTATS)
          double t2 = toc();
#endif
#ifdef _VERBOSE
          INFO << subProblemforH->getSolutionVector();
#endif
                        this->H.row(i) = subProblemforH.getSolutionVector().t();
#ifdef _VERBOSE
          INFO << "Comp H(" << i << "/" << this->n
               << ") of it=" << currentIteration << " time taken=" << t2
               << " num_iterations()=" << numIter << std::endl;
#endif
                    }
                }
#ifdef _VERBOSE
      INFO << "H: at it = " << currentIteration << std::endl << this->H;
#endif
                {
                    // clear previous allocations.
                    WtW.clear();
                    WtA.clear();
                    arma::mat Ht = this->H.t();
                    arma::mat HtH = Ht * this->H;
                    this->applyReg(this->regW(), &this->HtH);
                    arma::mat HtAt = Ht * At;
                    Ht.clear();
                    // solve for W given H;
#pragma omp parallel for schedule(dynamic) default(none) num_threads(this->ncores)
                    for (int i = 0; i < this->m; i++) {
                        auto subProblemforW =
                                BPPNNLS<arma::mat, arma::vec>(HtH, static_cast<arma::vec>(HtAt.col(i)), true);
#ifdef _VERBOSE
          INFO << "Initialized subproblem and calling solveNNLS for "
               << "W(" << i << "/" << this->m << ")";
#endif
#if defined(_VERBOSE) || defined(COLLECTSTATS)
          tic();
          int numIter = subProblemforW->solveNNLS();
#else
                        subProblemforW.solveNNLS();
#endif
                        //     int numIter = subProblemforW->solveNNLS();
#if defined(_VERBOSE) || defined(COLLECTSTATS)
          double t2 = toc();
#endif
#ifdef _VERBOSE
          INFO << subProblemforW->getSolutionVector();
#endif

                        this->W.row(i) = subProblemforW.getSolutionVector().t();
#ifdef _VERBOSE
          INFO << "Comp W(" << i << "/" << this->n
               << ") of it=" << currentIteration << " time taken=" << t2
               << " num_iterations()=" << numIter << std::endl;
#endif
                    }
                    HtH.clear();
                    HtAt.clear();
                }
#ifdef _VERBOSE
      INFO << "W: at it = " << currentIteration << std::endl << this->W;
#endif
#ifdef COLLECTSTATS
                // INFO << "iteration = " << currentIteration << " currentObjectiveError="
                // << this->objective_err << std::endl;
#endif
                currentIteration++;
            }
        }

        void computeNMF() override {
#ifdef COLLECTSTATS
            // this->objective_err;
#endif
#ifdef _VERBOSE
    INFO << PRINTMATINFO(this->At);
    INFO << "Starting BPP for num_iterations()=" << this->num_iterations()
         << std::endl;
#endif
            this->commonSolve();
        };

        double getObjectiveError() { return this->objectiveErr; }

        /*
         * I dont like this function here. But this seems to be the
         * easy place for having it. This function really should have been
         * in BPPNNLS.hpp. It will take some time to refactor this.
         * Given, A and W, solve for H.
         */
        arma::mat solveScalableNNLS() {
            updateOtherGivenOneMultipleRHS(this->A, this->W, 'H', &(this->H));
            return this->H;
        }

        ~BPPNMF() override { this->At.clear(); }
    }; // class BPPNMF

    //template<>
    //void BPPNMF<arma::sp_mat>::computeNMF() = delete;
} // namespace planc
