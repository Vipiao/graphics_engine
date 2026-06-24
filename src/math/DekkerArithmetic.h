#pragma once

#include <iostream>
#include <cmath>
#include <vector>
#include <random>
#include <numeric>
#include <algorithm>
#include <functional>

template<typename T>
class DekkerArithmetic {
public:
   static constexpr T calculateScale() {
      // Based on the type of T, calculate the scale factor to be used in the Dekker algorithm.
      // This is 2^round_up(digits_of_mantissa/2) + 1
      constexpr auto mantissa_bits = std::numeric_limits<T>::digits;
      // Round up division by 2
      constexpr auto shift_amount = (mantissa_bits + (mantissa_bits % 2)) / 2;
      if constexpr (std::is_same_v<T, float>) {
         return static_cast<T>((static_cast<uint32_t>(1) << shift_amount) + 1);
      } else if constexpr (std::is_same_v<T, double>) {
         return static_cast<T>((static_cast<uint64_t>(1) << shift_amount) + 1);
      } else {
         static_assert(std::is_floating_point_v<T>, "DekkerArithmetic requires a floating-point type.");
         return T{};
      }
   }
   static constexpr T SCALE = calculateScale();
   struct DekkerNumber {
      T main;
      T error;

      explicit DekkerNumber() : main(0), error(0) {}

      explicit DekkerNumber(T mainPart, T errorPart) : main(mainPart), error(errorPart) {}

      explicit DekkerNumber(float value) : main(value), error(0) {}

      explicit DekkerNumber(double value) {
         main = static_cast<T>(value);
         error = static_cast<T>(value - static_cast<double>(main));
      }
   };

   static DekkerNumber add(const DekkerNumber& x, const DekkerNumber& y) {
      // The main parts are added
      T R{ x.main + y.main };
      T r{};

      // Adjust the error parts
      if (std::abs(x.main) > std::abs(y.main)) {
         r = x.main - R + y.main + y.error + x.error;
      } else {
         r = y.main - R + x.main + x.error + y.error;
      }

      // Sum the main and error parts
      T mainPart{ R + r };
      T errorPart{ R - mainPart + r };

      return DekkerNumber{ mainPart, errorPart };
   }

   static DekkerNumber multiply(const DekkerNumber& x, const DekkerNumber& y) {
      T p00{}, p01{}, p10{}, p11{}, e00{}, e01{}, e10{}, e11{};

      e00 = exactProduct(x.main, y.main, p00);
      e01 = exactProduct(x.main, y.error, p01);
      e10 = exactProduct(x.error, y.main, p10);
      e11 = exactProduct(x.error, y.error, p11);

      T mainPart = p00;
      T errorPart = e00 + p11 + (p01 + p10);
      return DekkerNumber{ mainPart, errorPart };
   }

   static DekkerNumber Split(T a) {
      DekkerNumber r{};
      T p = a * SCALE;
      r.main = a - p + p;
      r.error = a - r.main;
      return r;
   }

   static DekkerNumber DekkerMul12(T a, T b) {
      DekkerNumber A{ Split(a) };
      DekkerNumber B{ Split(b) };
      T p{ A.main * B.main };
      T q{ A.main * B.error + A.error * B.main };
      DekkerNumber R{};
      R.main = p + q;
      R.error = p - R.main + q + A.error * B.error;
      return R;
   }

   static DekkerNumber divide(const DekkerNumber& a, const DekkerNumber& b) {
      DekkerNumber u{};
      u.main = a.main / b.main;
      DekkerNumber t{ DekkerMul12(u.main, b.main)};
      T l{ (a.main - t.main - t.error + a.error - u.main * b.error) / b.main };
      DekkerNumber r{};
      r.main = u.main + l;
      r.error = u.main - r.main + l;
      return r;
   }

   static DekkerNumber sqrt(const DekkerNumber& x) {
      if (x.main > 0) {
         // Compute the square root of the main part
         T c = std::sqrt(x.main);

         // Compute the square root using Dekker multiplication for extended precision
         DekkerNumber cDekker = DekkerMul12(c, c);

         // Calculate the error correction term cc
         T cc = ((x.main - cDekker.main - cDekker.error + x.error) * static_cast<T>(0.5)) / c;

         // Calculate the high precision result
         DekkerNumber y;
         y.main = c + cc;
         y.error = c - y.main + cc;

         return y;
      } else {
         // For x.main <= 0, we cannot take the square root
         return DekkerNumber{ 0, 0 };
      }
   }

   //static DekkerNumber sqrt(const DekkerNumber& x) {
   //   T mainPart{ std::sqrt(x.main) };
   //   T fma_result{ std::fma(x.main, -mainPart * mainPart, x.error) };
   //   T errorPart{ fma_result / (2 * mainPart) };
   //
   //   return DekkerNumber{ mainPart, errorPart };
   //}

   static DekkerNumber abs(const DekkerNumber& x) {
      if (x.main < 0) {
         return DekkerNumber{ -x.main, -x.error };
      }
      return x;
   }

   static float toFloat(const DekkerNumber& x) {
      return x.main + x.error;
   }

   static double toDouble(const DekkerNumber& x) {
      return static_cast<double>(x.main) + static_cast<double>(x.error);
   }

   static void unitTest() {
      std::vector<double> dekkerErrorsAdditionABS{};
      std::vector<double> floatErrorsAdditionABS{};
      std::vector<double> dekkerErrorsMultiplicationABS{};
      std::vector<double> floatErrorsMultiplicationABS{};
      std::vector<double> dekkerErrorsDivisionABS{};
      std::vector<double> floatErrorsDivisionABS{};
      std::vector<double> dekkerErrorsSqrtABS{};
      std::vector<double> floatErrorsSqrtABS{};

      std::vector<double> dekkerErrorsAdditionFrac{};
      std::vector<double> floatErrorsAdditionFrac{};
      std::vector<double> dekkerErrorsMultiplicationFrac{};
      std::vector<double> floatErrorsMultiplicationFrac{};
      std::vector<double> dekkerErrorsDivisionFrac{};
      std::vector<double> floatErrorsDivisionFrac{};
      std::vector<double> dekkerErrorsSqrtFrac{};
      std::vector<double> floatErrorsSqrtFrac{};

      std::vector<double> nums1_double(10000);
      std::vector<double> nums2_double(10000);
      std::random_device rd;
      std::mt19937 gen(rd());
      std::uniform_real_distribution<> dis(-100.0, 100.0);
      // Set seed.
      gen.seed(0);
      std::generate(nums1_double.begin(), nums1_double.end(), [&] { return dis(gen); });
      std::generate(nums2_double.begin(), nums2_double.end(), [&] { return dis(gen); });

      std::vector<double> specialNumbers{
         0.0, 1.0, -1.0,
         std::numeric_limits<double>::infinity(),
         -std::numeric_limits<double>::infinity(),
         std::numeric_limits<double>::quiet_NaN(),
         std::numeric_limits<double>::signaling_NaN(),
         std::numeric_limits<double>::denorm_min(),
         std::numeric_limits<double>::min(),
         std::numeric_limits<double>::max(),
         std::numeric_limits<double>::lowest(),
         std::numeric_limits<double>::denorm_min() * 2,
      };
      size_t numIterations{ specialNumbers.size() };
      for (size_t ii = 0; ii < numIterations; ii++) {
         specialNumbers.push_back(-specialNumbers[ii]);
         specialNumbers.push_back(specialNumbers[ii] * 2);
         specialNumbers.push_back(specialNumbers[ii] / 2);
         specialNumbers.push_back(specialNumbers[ii] + 1);
         specialNumbers.push_back(specialNumbers[ii] - 1);
         specialNumbers.push_back(specialNumbers[ii] + 0.1);
         specialNumbers.push_back(specialNumbers[ii] - 0.1);
         specialNumbers.push_back(specialNumbers[ii] * 0.1);
         specialNumbers.push_back(specialNumbers[ii] / 0.1);
         specialNumbers.push_back(specialNumbers[ii] * 10);
         specialNumbers.push_back(specialNumbers[ii] / 10);
      }
      for (size_t ii = 0; ii < specialNumbers.size(); ii++) {
         for (size_t jj = 0; jj < specialNumbers.size(); jj++) {
            nums1_double.push_back(specialNumbers[ii]);
            nums2_double.push_back(specialNumbers[jj]);
         }
      }

      for (int ii = 0; ii < nums1_double.size(); ++ii) {
         double num1_double = nums1_double[ii];
         double num2_double = nums2_double[ii];

         float num1_float = static_cast<float>(num1_double);
         float num2_float = static_cast<float>(num2_double);

         DekkerArithmetic<float>::DekkerNumber num1_dekker{ num1_double };
         DekkerArithmetic<float>::DekkerNumber num2_dekker{ num2_double };

         // Addition.
         {
            double result_double = num1_double + num2_double;
            float result_float = num1_float + num2_float;
            auto result_dekker = DekkerArithmetic<float>::add(num1_dekker, num2_dekker);

            double dekkerErrorABS = std::abs(result_double - DekkerArithmetic::toDouble(result_dekker));
            double floatErrorABS = std::abs(result_double - result_float);

            dekkerErrorsAdditionABS.push_back(dekkerErrorABS);
            floatErrorsAdditionABS.push_back(floatErrorABS);

            double dekkerErrorFrac = dekkerErrorABS / std::abs(result_double);
            double floatErrorFrac = floatErrorABS / std::abs(result_double);

            dekkerErrorsAdditionFrac.push_back(dekkerErrorFrac);
            floatErrorsAdditionFrac.push_back(floatErrorFrac);
         }
         // Multiplication.
         {
            double result_double = num1_double * num2_double;
            float result_float = num1_float * num2_float;
            auto result_dekker = DekkerArithmetic<float>::multiply(num1_dekker, num2_dekker);

            double dekkerErrorABS = std::abs(result_double - DekkerArithmetic::toDouble(result_dekker));
            double floatErrorABS = std::abs(result_double - result_float);

            dekkerErrorsMultiplicationABS.push_back(dekkerErrorABS);
            floatErrorsMultiplicationABS.push_back(floatErrorABS);

            double dekkerErrorFrac = dekkerErrorABS / std::abs(result_double);
            double floatErrorFrac = floatErrorABS / std::abs(result_double);

            dekkerErrorsMultiplicationFrac.push_back(dekkerErrorFrac);
            floatErrorsMultiplicationFrac.push_back(floatErrorFrac);
         }
         // Division.
         {
            double result_double = num1_double / num2_double;
            float result_float = num1_float / num2_float;
            auto result_dekker = DekkerArithmetic<float>::divide(num1_dekker, num2_dekker);

            double dekkerErrorABS = std::abs(result_double - DekkerArithmetic::toDouble(result_dekker));
            double floatErrorABS = std::abs(result_double - result_float);

            dekkerErrorsDivisionABS.push_back(dekkerErrorABS);
            floatErrorsDivisionABS.push_back(floatErrorABS);

            double dekkerErrorFrac = dekkerErrorABS / std::abs(result_double);
            double floatErrorFrac = floatErrorABS / std::abs(result_double);

            dekkerErrorsDivisionFrac.push_back(dekkerErrorFrac);
            floatErrorsDivisionFrac.push_back(floatErrorFrac);
         }
         // Square root. But abs is used on negative numbers.
         {
            double result_double = std::sqrt(std::abs(num1_double));
            float result_float = std::sqrt(std::abs(num1_float));
            auto result_dekker = DekkerArithmetic<float>::sqrt(DekkerArithmetic::abs(num1_dekker));

            double dekkerErrorABS = std::abs(result_double - DekkerArithmetic::toDouble(result_dekker));
            double floatErrorABS = std::abs(result_double - result_float);

            dekkerErrorsSqrtABS.push_back(dekkerErrorABS);
            floatErrorsSqrtABS.push_back(floatErrorABS);

            double dekkerErrorFrac = dekkerErrorABS / std::abs(result_double);
            double floatErrorFrac = floatErrorABS / std::abs(result_double);

            dekkerErrorsSqrtFrac.push_back(dekkerErrorFrac);
            floatErrorsSqrtFrac.push_back(floatErrorFrac);
         }
      }

      std::vector<std::string> operations{ "Addition", "Multiplication", "Division", "Square Root" };
      std::vector<std::vector<double>> dekkerErrorsABS{ dekkerErrorsAdditionABS, dekkerErrorsMultiplicationABS, dekkerErrorsDivisionABS, dekkerErrorsSqrtABS };
      std::vector<std::vector<double>> floatErrorsABS{ floatErrorsAdditionABS, floatErrorsMultiplicationABS, floatErrorsDivisionABS, floatErrorsSqrtABS };
      std::vector<std::vector<double>> dekkerErrorsFrac{ dekkerErrorsAdditionFrac, dekkerErrorsMultiplicationFrac, dekkerErrorsDivisionFrac, dekkerErrorsSqrtFrac };
      std::vector<std::vector<double>> floatErrorsFrac{ floatErrorsAdditionFrac, floatErrorsMultiplicationFrac, floatErrorsDivisionFrac, floatErrorsSqrtFrac };

      for (size_t ii = 0; ii < operations.size(); ii++) {
         double meanDekkerErrorABS = mean(dekkerErrorsABS[ii]);
         double stdDevDekkerErrorABS = stddev(dekkerErrorsABS[ii], meanDekkerErrorABS);

         double meanFloatErrorABS = mean(floatErrorsABS[ii]);
         double stdDevFloatErrorABS = stddev(floatErrorsABS[ii], meanFloatErrorABS);

         double meanDekkerErrorFrac = mean(dekkerErrorsFrac[ii]);
         double stdDevDekkerErrorFrac = stddev(dekkerErrorsFrac[ii], meanDekkerErrorFrac);

         double meanFloatErrorFrac = mean(floatErrorsFrac[ii]);
         double stdDevFloatErrorFrac = stddev(floatErrorsFrac[ii], meanFloatErrorFrac);

         std::cout << operations[ii] << ":" << std::endl;
         std::cout << "Dekker Arithmetic Mean Error ABS: " << meanDekkerErrorABS << ", Std Dev ABS: " << stdDevDekkerErrorABS << std::endl;
         std::cout << "Float Arithmetic Mean Error ABS: " << meanFloatErrorABS << ", Std Dev ABS: " << stdDevFloatErrorABS << std::endl;
         bool foundLargerError = false;
         for (size_t jj = 0; jj < dekkerErrorsABS[ii].size(); jj++) {
            double dekkerErrorABS = dekkerErrorsABS[ii][jj];
            double floatErrorABS = floatErrorsABS[ii][jj];
            if (dekkerErrorABS > floatErrorABS) {
               foundLargerError = true;
               std::cout << "At least one Dekker error ABS is greater than aritmetic error" << std::endl;
               break;
            }
         }
         // Calculate largest error.
         double largestDekkerErrorABS{ -std::numeric_limits<double>::infinity() };
         for (size_t jj = 0; jj < dekkerErrorsABS[ii].size(); jj++) {
            double dekkerErrorABS = dekkerErrorsABS[ii][jj];
            if (dekkerErrorABS > largestDekkerErrorABS) {
               largestDekkerErrorABS = dekkerErrorABS;
            }
         }
         std::cout << "Largest Dekker Error ABS: " << largestDekkerErrorABS << std::endl;
         std::cout << std::endl;

         std::cout << "Dekker Arithmetic Mean Error Frac: " << meanDekkerErrorFrac << ", Std Dev Frac: " << stdDevDekkerErrorFrac << std::endl;
         std::cout << "Float Arithmetic Mean Error Frac: " << meanFloatErrorFrac << ", Std Dev Frac: " << stdDevFloatErrorFrac << std::endl;
         foundLargerError = false;
         for (size_t jj = 0; jj < dekkerErrorsFrac[ii].size(); jj++) {
            double dekkerErrorFrac = dekkerErrorsFrac[ii][jj];
            double floatErrorFrac = floatErrorsFrac[ii][jj];
            if (dekkerErrorFrac > floatErrorFrac) {
               foundLargerError = true;
               std::cout << "At least one Dekker error Frac is greater than aritmetic error" << std::endl;
               break;
            }
         }
         // Calculate largest error.
         double largestDekkerErrorFrac{ -std::numeric_limits<double>::infinity() };
         for (size_t jj = 0; jj < dekkerErrorsFrac[ii].size(); jj++) {
            double dekkerErrorFrac = dekkerErrorsFrac[ii][jj];
            if (dekkerErrorFrac > largestDekkerErrorFrac) {
               largestDekkerErrorFrac = dekkerErrorFrac;
            }
         }
         std::cout << "Largest Dekker Error Frac: " << largestDekkerErrorFrac << std::endl;
         std::cout << std::endl;
      }
   }

private:
   // Helper function to calculate the rounding error of a float/double addition
   static T err(T a, T b) {
      volatile T temp = a + b;
      return (a - temp) + b;
   }

   // Helper function to perform exact product and return the error term
   static T exactProduct(T a, T b, T& product) {
      product = a * b;
      return std::fma(a, b, -product);
   }

   // Function to calculate the mean of a vector
   template<typename U>
   static U mean(const std::vector<U>& v) {
      U sum = std::accumulate(v.begin(), v.end(), static_cast<U>(0),
         [](const U& total, const U& value) {
            // Use only finite numbers in the sum
            return std::isfinite(value) ? total + value : total;
         }
      );
      size_t count = std::count_if(v.begin(), v.end(), [](const U& value) { return std::isfinite(value); });
      return count > 0 ? sum / static_cast<U>(count) : static_cast<U>(0);
   }

   template<typename U>
   static U stddev(const std::vector<U>& v, U mean) {
      U sum = std::accumulate(v.begin(), v.end(), static_cast<U>(0),
         [mean](const U& total, const U& value) {
            // Use only finite numbers in the sum of squares
            return std::isfinite(value) ? total + (value - mean) * (value - mean) : total;
         }
      );
      size_t count = std::count_if(v.begin(), v.end(), [](const U& value) { return std::isfinite(value); });
      return count > 1 ? std::sqrt(sum / static_cast<U>(count - 1)) : static_cast<U>(0);
   }

};
