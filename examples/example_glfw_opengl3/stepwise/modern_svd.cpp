#include "modern_svd.h"
#include <cmath>
#include <algorithm>
#include <stdexcept>

// FAITHFUL PORT of SVDCMP.CPP - NO "IMPROVEMENTS" OR "SIMPLIFICATIONS"
// Every floating-point operation must match exactly for identical numerical results

// Helper function to compute sqrt(a^2 + b^2) without destructive overflow or underflow.
double ModernSVD::pythag(double a, double b) {
    double absa = std::abs(a);
    double absb = std::abs(b);
    if (absa > absb) {
        double ratio = absb / absa;
        return absa * std::sqrt(1.0 + ratio * ratio);
    }
    if (absb == 0.0) {
        return 0.0;
    }
    double ratio = absa / absb;
    return absb * std::sqrt(1.0 + ratio * ratio);
}

ModernSVD::ModernSVD(int n_rows, int n_cols, bool save_a_matrix) {
    if (n_cols > n_rows) {
        ok = false;
        n_rows_ = n_cols_ = 0;
        return;
    }
    n_rows_ = n_rows;
    n_cols_ = n_cols;
    try {
        a.resize(n_rows * n_cols);
        w.resize(n_cols);
        v.resize(n_cols * n_cols);
        b.resize(n_rows);
        work.resize(n_cols);
        if (save_a_matrix) {
            u.resize(n_rows * n_cols);
        }
    } catch (const std::bad_alloc&) {
        ok = false;
        return;
    }
    ok = true;
}

void ModernSVD::decompose() {
    int sval, split, iter_limit;
    double* matrix = u.empty() ? a.data() : u.data();

    if (!u.empty()) {
        std::copy(a.begin(), a.end(), u.begin());
    }

    bidiag();
    right();
    left();

    sval = n_cols_;
    while (sval--) {
        iter_limit = 50;
        while (iter_limit--) {
            split = sval + 1;
            while (--split) {
                if (norm + std::abs(work[split]) == norm) {
                    break;
                }
                if (norm + std::abs(w[split - 1]) == norm) {
                    cancel(split, sval);
                    break;
                }
            }
            if (split == sval) {
                if (w[sval] < 0.0) {
                    w[sval] = -w[sval];
                    for (int i = 0; i < n_cols_; i++) {
                        v[i * n_cols_ + sval] = -v[i * n_cols_ + sval];
                    }
                }
                break;
            }
            qr(split, sval);
        }
    }
}

void ModernSVD::bidiag() {
    int k;
    double temp, scale;
    double* matrix = u.empty() ? a.data() : u.data();

    norm = temp = scale = 0.0;

    for (int col = 0; col < n_cols_; col++) {
        work[col] = scale * temp;
        scale = 0.0;
        for (k = col; k < n_rows_; k++) {
            scale += std::abs(matrix[k * n_cols_ + col]);
        }
        if (scale > 0.0) {
            w[col] = scale * bid1(col, scale);
        } else {
            w[col] = 0.0;
        }

        scale = 0.0;
        for (k = col + 1; k < n_cols_; k++) {
            scale += std::abs(matrix[col * n_cols_ + k]);
        }
        if (scale > 0.0) {
            temp = bid2(col, scale);
        } else {
            temp = 0.0;
        }

        double testnorm = std::abs(w[col]) + std::abs(work[col]);
        if (testnorm > norm) {
            norm = testnorm;
        }
    }
}

// CRITICAL FIX: Use exact legacy formula - fac = 1.0 / (diag * rv - sum)
double ModernSVD::bid1(int col, double scale) {
    int i, j;
    double diag, rv, fac, sum;
    double* matrix = u.empty() ? a.data() : u.data();

    sum = 0.0;
    for (i = col; i < n_rows_; i++) {
        fac = (matrix[i * n_cols_ + col] /= scale);
        sum += fac * fac;
    }
    rv = std::sqrt(sum);
    diag = matrix[col * n_cols_ + col];
    if (diag > 0.0)
        rv = -rv;
    fac = 1.0 / (diag * rv - sum);  // EXACT LEGACY FORMULA - NO SIMPLIFICATION
    matrix[col * n_cols_ + col] = diag - rv;

    for (j = col + 1; j < n_cols_; j++) {
        sum = 0.0;
        for (i = col; i < n_rows_; i++)
            sum += matrix[i * n_cols_ + col] * matrix[i * n_cols_ + j];
        sum *= fac;
        for (i = col; i < n_rows_; i++)
            matrix[i * n_cols_ + j] += sum * matrix[i * n_cols_ + col];
    }

    for (i = col; i < n_rows_; i++)
        matrix[i * n_cols_ + col] *= scale;
    return rv;
}

// CRITICAL FIX: Use exact legacy formula - fac = 1.0 / (diag * rv - sum)  
double ModernSVD::bid2(int col, double scale) {
    int i, j;
    double fac, diag, rv, sum;
    double* matrix = u.empty() ? a.data() : u.data();

    sum = 0.0;
    for (i = col + 1; i < n_cols_; i++) {
        fac = (matrix[col * n_cols_ + i] /= scale);
        sum += fac * fac;
    }

    rv = std::sqrt(sum);
    diag = matrix[col * n_cols_ + col + 1];
    if (diag > 0.0)
        rv = -rv;

    matrix[col * n_cols_ + col + 1] = diag - rv;
    fac = 1.0 / (diag * rv - sum);  // EXACT LEGACY FORMULA - NO SIMPLIFICATION
    for (i = col + 1; i < n_cols_; i++)
        work[i] = fac * matrix[col * n_cols_ + i];

    for (j = col + 1; j < n_rows_; j++) {
        sum = 0.0;
        for (i = col + 1; i < n_cols_; i++)
            sum += matrix[j * n_cols_ + i] * matrix[col * n_cols_ + i];
        for (i = col + 1; i < n_cols_; i++)
            matrix[j * n_cols_ + i] += sum * work[i];
    }
    for (i = col + 1; i < n_cols_; i++)
        matrix[col * n_cols_ + i] *= scale;
    return rv;
}

void ModernSVD::right() {
    int col, i, j;
    double temp, denom, sum;
    double* matrix = u.empty() ? a.data() : u.data();

    denom = 0.0;
    col = n_cols_;
    while (col--) {
        if (denom != 0.0) {
            temp = 1.0 / matrix[col * n_cols_ + col + 1];
            for (i = col + 1; i < n_cols_; i++)  // Double division avoids underflow
                v[i * n_cols_ + col] = temp * matrix[col * n_cols_ + i] / denom;
            for (i = col + 1; i < n_cols_; i++) {
                sum = 0.0;
                for (j = col + 1; j < n_cols_; j++)
                    sum += v[j * n_cols_ + i] * matrix[col * n_cols_ + j];
                for (j = col + 1; j < n_cols_; j++)
                    v[j * n_cols_ + i] += sum * v[j * n_cols_ + col];
            }
        }

        denom = work[col];

        for (i = col + 1; i < n_cols_; i++)
            v[col * n_cols_ + i] = v[i * n_cols_ + col] = 0.0;
        v[col * n_cols_ + col] = 1.0;
    }
}

void ModernSVD::left() {
    int col, i, j;
    double temp, fac, sum;
    double* matrix = u.empty() ? a.data() : u.data();
    col = n_cols_;
    while (col--) {
        for (i = col + 1; i < n_cols_; i++)
            matrix[col * n_cols_ + i] = 0.0;
        if (w[col] != 0.0) {
            fac = 1.0 / w[col];
            temp = fac / matrix[col * n_cols_ + col];
            for (i = col + 1; i < n_cols_; i++) {
                sum = 0.0;
                for (j = col + 1; j < n_rows_; j++)
                    sum += matrix[j * n_cols_ + col] * matrix[j * n_cols_ + i];
                sum *= temp;
                for (j = col; j < n_rows_; j++)
                    matrix[j * n_cols_ + i] += sum * matrix[j * n_cols_ + col];
            }
            for (i = col; i < n_rows_; i++)
                matrix[i * n_cols_ + col] *= fac;
        } else {
            for (i = col; i < n_rows_; i++)
                matrix[i * n_cols_ + col] = 0.0;
        }
        matrix[col * n_cols_ + col] += 1.0;
    }
}

// CRITICAL FIX: Faithful port of legacy cancel function 
void ModernSVD::cancel(int low, int high) {
    int col, row, lm1;
    double sine, cosine, leg1, leg2, svhypot, y, x, *mpt1, *mpt2;
    double* matrix = u.empty() ? a.data() : u.data();

    lm1 = low - 1;
    sine = 1.0;  // Legacy only initializes sine, not cosine
    for (col = low; col <= high; col++) {
        leg1 = sine * work[col];
        if (std::abs(leg1) + norm != norm) {
            leg2 = w[col];
            w[col] = svhypot = pythag(leg1, leg2);
            sine = -leg1 / svhypot;
            cosine = leg2 / svhypot;
            for (row = 0; row < n_rows_; row++) {
                mpt1 = matrix + row * n_cols_ + col;
                mpt2 = matrix + row * n_cols_ + lm1;
                x = *mpt1;
                y = *mpt2;
                *mpt1 = x * cosine - y * sine;
                *mpt2 = x * sine + y * cosine;
            }
        }
    }
}

void ModernSVD::qr(int low, int high) {
    int col;
    double sine, cosine, wk, tx, ty, x, y, svhypot, temp, ww, wh, wkh, whm1, wkhm1;
    double* matrix = u.empty() ? a.data() : u.data();

    wh = w[high];
    whm1 = w[high - 1];
    wkh = work[high];
    wkhm1 = work[high - 1];
    temp = 2.0 * wkh * whm1;
    if (temp != 0.0)
        temp = ((whm1 + wh) * (whm1 - wh) + (wkhm1 + wkh) * (wkhm1 - wkh)) / temp;
    else
        temp = 0.0;

    svhypot = pythag(temp, 1.0);
    if (temp < 0.0)
        svhypot = -svhypot;

    ww = w[low];
    wk = wkh * (whm1 / (temp + svhypot) - wkh) + (ww + wh) * (ww - wh);
    if (ww != 0.0)
        wk /= ww;
    else
        wk = 0.0;

    sine = cosine = 1.0;

    for (col = low; col < high; col++) {
        x = work[col + 1];
        ty = sine * x;
        x *= cosine;
        svhypot = pythag(wk, ty);
        work[col] = svhypot;
        cosine = wk / svhypot;
        sine = ty / svhypot;
        tx = ww * cosine + x * sine;
        x = x * cosine - ww * sine;
        y = w[col + 1];
        ty = y * sine;
        y *= cosine;
        qr_vrot(col, sine, cosine);
        w[col] = svhypot = pythag(tx, ty);
        if (svhypot != 0.0) {
            cosine = tx / svhypot;
            sine = ty / svhypot;
        }
        qr_mrot(col, sine, cosine);
        wk = cosine * x + sine * y;
        ww = cosine * y - sine * x;
    }
    work[low] = 0.0;
    work[high] = wk;
    w[high] = ww;
}


void ModernSVD::qr_vrot(int col, double sine, double cosine) {
    int row;
    double x, y, * vptr;
    for (row = 0; row < n_cols_; row++) {
        vptr = v.data() + row * n_cols_ + col;
        x = *vptr;
        y = *(vptr + 1);
        *vptr = x * cosine + y * sine;
        *(vptr + 1) = y * cosine - x * sine;
    }
}

void ModernSVD::qr_mrot(int col, double sine, double cosine) {
    int row;
    double x, y, * mptr;
    double* matrix = u.empty() ? a.data() : u.data();
    for (row = 0; row < n_rows_; row++) {
        mptr = matrix + row * n_cols_ + col;
        x = *mptr;
        y = *(mptr + 1);
        *mptr = x * cosine + y * sine;
        *(mptr + 1) = y * cosine - x * sine;
    }
}

void ModernSVD::back_substitute(double threshold, double* solution) {
    int i, j;
    double sum, wmax;
    double* matrix = u.empty() ? a.data() : u.data();

    wmax = -1.e40;
    for (i = 0; i < n_cols_; i++) {
        if ((i == 0) || (w[i] > wmax))
            wmax = w[i];
    }

    threshold = threshold * wmax + 1.e-60;

    for (i = 0; i < n_cols_; i++) {
        sum = 0.0;
        if (w[i] > threshold) {
            for (j = 0; j < n_rows_; j++)
                sum += matrix[j * n_cols_ + i] * b[j];
            sum /= w[i];
        }
        work[i] = sum;
    }

    for (i = 0; i < n_cols_; i++) {
        sum = 0.0;
        for (j = 0; j < n_cols_; j++)
            sum += v[i * n_cols_ + j] * work[j];
        solution[i] = sum;
    }
}