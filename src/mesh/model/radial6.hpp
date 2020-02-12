/*
 * Copyright (C) 2017-2018 Trent Houliston <trent@houliston.me>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef VISUALMESH_MODEL_RADIAL6_HPP
#define VISUALMESH_MODEL_RADIAL6_HPP

#include <array>
#include <fstream>
#include <iostream>
#include <vector>

#include "mesh/node.hpp"

namespace visualmesh {
namespace model {

  template <typename Scalar>
  struct Radial6 {
  private:
    static inline vec3<Scalar> unit_vector(const Scalar& sin_phi, const Scalar& cos_phi, const Scalar& theta) {
      return vec3<Scalar>{{std::cos(theta) * sin_phi, std::sin(theta) * sin_phi, -cos_phi}};
    }

  public:
    static constexpr size_t N_NEIGHBOURS = 6;

    template <typename Shape>
    static std::vector<Node<Scalar, N_NEIGHBOURS>> generate(const Shape& shape,
                                                            const Scalar& h,
                                                            const Scalar& k,
                                                            const Scalar& max_distance) {
      std::vector<Node<Scalar, N_NEIGHBOURS>> nodes;
      // Stores the number of points in each ring
      std::vector<int> number_points;

      // L, TL, TR, R, BR, BL
      // add easily configurable neighbour ordering
      int LEFT       = 0;
      int TOPLEFT    = 1;
      int TOPRIGHT   = 2;
      int RIGHT      = 3;
      int BELOWRIGHT = 4;
      int BELOWLEFT  = 5;

      // Add origin point to node vector
      // ?the left and right neighbours may need to be switched, pending experiment.
      nodes.push_back(Node<Scalar, N_NEIGHBOURS>{{0, 0, -1}, {3, 2, 1, 6, 5, 4}});

      // Stores the number of points for the origin patch
      std::vector<int> origin_number_points;
      // Add the one point for the origin
      origin_number_points.emplace_back(1);


      // Initiate first ring
      Scalar phi_first = shape.phi(1 / k, h);
      // Eight points in the first ring, all connect below to the origin
      for (int j = 0; j < 6; ++j) {
        Node<Scalar, N_NEIGHBOURS> first;
        first.ray = unit_vector(std::sin(phi_first), std::cos(phi_first), (j - 1 / 2) * (2 * M_PI / 6));
        first.neighbours[BELOWRIGHT] = 0;
        first.neighbours[BELOWLEFT]  = 0;
        first.neighbours[LEFT]       = 1 + ((j + 1) % 6 + 6) % 6;
        first.neighbours[RIGHT]      = 1 + ((j - 1) % 6 + 6) % 6;
        nodes.push_back(std::move(first));
      }
      number_points.emplace_back(6);

      // Stores the last index in order to loop through the last row to generate the next row
      int running_index = nodes.size();

      // The variable stopping row for the origin patch
      int stop;
      if (k < 9) { stop = 5; }
      else {
        stop = 8;
      }

      // The number of points in each ring to create the origin patch
      origin_number_points.emplace_back(12);
      origin_number_points.emplace_back(24);

      for (int i = 4; i < 4 + stop; ++i) {
        origin_number_points.emplace_back(8 * i);
      }

      bool half_offset = false;

      // Generate the rest of the mesh, v must be int
      for (int v = 1; h * std::tan(shape.phi(v / k, h)) < max_distance; ++v) {
        // Calculates the phi value of the ring that is being generated
        // The int to scalar conversion here will work since k is scalar
        Scalar phi_next = shape.phi((v + 1) / k, h);
        // Calculates the index in nodes of the first node on the last ring
        int begin = running_index - number_points.back();
        // The index where last ring stops
        int end = running_index;

        // Avoids consecutive splits
        bool every_one = false;

        // Handles the addition of only a single point between rings
        bool growing = false;

        // Odd v generates clockwise, even v generates anti-clockwise
        int one = (v % 2 == 0 ? 1 : 1);
        // Number of points in the last ring
        int number_points_now = number_points[v - 1];
        // Number of points in the next ring
        int number_points_next;

        // Calculate the number of points on the next ring according to dtheta or the origin patch
        if (v < stop) { number_points_next = origin_number_points[v]; }
        else {
          number_points_next = std::ceil((2 * M_PI * k) / shape.theta(phi_next, h));
        }
        // Find the difference between the generating ring and ring to be generated
        int number_difference = number_points_next - number_points_now;

        // dtheta of the next ring
        Scalar theta_next;

        // *************** Calculate Split Distribution ***********************
        int distribution;
        if (number_difference == 0) {
          // Difference is zero
          distribution       = 0;
          number_points_next = number_points_now;
        }
        else if (number_difference == 1) {
          // Difference is 1
          growing      = true;
          distribution = 1;
          // number_difference = 1;
          // Although this may be redundant atm I think it is necessary
          number_points_next = 1 + number_points_now;
        }
        else if (number_difference > 1) {
          growing = true;
          if (number_difference < number_points_now) {
            // Difference divides the number of points in the generating ring
            distribution = number_points_now / number_difference;
            // Difference does not divide the number of points in the generating ring
            if (distribution == 1) { every_one = true; }
          }
          else {
            // number_difference >= number_points_now
            // Difference is greater than or equal to the number of points in the generating ring
            distribution       = 1;
            number_points_next = 2 * number_points_now;
            number_difference  = number_points_now;
          }
        }
        else {
          // Difference is negative so maintain constant growth until the number of points is increasing
          // This case will happen sometimes (sometimes when connecting the origin patch)
          distribution       = 0;
          number_points_next = number_points_now;
        }
        // *********************************************************************************

        // Calculate theta_next instead of dtheta to correct for a change in number of points next based on the
        // distribution above
        theta_next = 2 * M_PI / number_points_next;
        // Store the number of points of the next ring
        number_points.emplace_back(number_points_next);

        // Gets the global indices of the last row
        std::vector<int> indices;
        for (int m = begin; m < end; ++m) {
          indices.push_back(m);
        }

        // Switches the offset of the splits to half the distribution when the distribution is greater than 2k + 2
        if (distribution >= 2 * k + 2) { half_offset = true; };

        // Chooses starting node
        int new_offset = 1;
        if (v == 1) { new_offset = 0; }
        else if (half_offset) {
          //?could use ceil
          new_offset = std::floor(distribution / 2);
        }
        else {
          new_offset = 1;
        }

        // Initiate relative indices within the ring that will be generated
        int relative_index_now  = 0;
        int relative_index_next = 0;
        // Keeps track of number of splits
        int number_splits = 0;
        // Gets the theta position of the starting node
        Scalar theta_offset = std::atan2(nodes[indices[new_offset]].ray[1], nodes[indices[new_offset]].ray[0]);

        // Loops through the nodes of the last ring to generate the new nodes
        for (size_t i = 0; i < indices.size(); ++i) {
          int it = (i + new_offset) % indices.size();
          // *************** Generate First Node ***********************
          Node<Scalar, N_NEIGHBOURS> new_node;
          new_node.ray =
            unit_vector(std::sin(phi_next), std::cos(phi_next), theta_offset + one * relative_index_next * theta_next);

          new_node.neighbours[LEFT] =
            end + (((relative_index_next + one) % number_points_next + number_points_next) % number_points_next);
          new_node.neighbours[RIGHT] =
            end + (((relative_index_next - one) % number_points_next + number_points_next) % number_points_next);
          new_node.neighbours[BELOWRIGHT] = indices[it];
          new_node.neighbours[BELOWLEFT]  = indices[(it + 1) % indices.size()];

          // Real equation: nodes[*it].neighbours[TOP] = end + relative_index_next % number_points_next;
          nodes[indices[it]].neighbours[TOPLEFT]  = end + relative_index_next;
          nodes[indices[it]].neighbours[TOPRIGHT] = end + (relative_index_next - 1) % number_points_next;
          nodes.push_back(std::move(new_node));
          relative_index_next += 1;
          // *********************************************************************************

          // *************** Generate Second Node ***********************
          // Add second node if a split
          if (growing) {
            if (every_one == true) {
              if (number_splits <= number_points_now - number_difference) { distribution = 2; }
              else {
                distribution = 1;
              }
            }

            // split every point of according to the distribution until difference is reached, or split every point.
            if ((relative_index_now % distribution == 0 || distribution == 1) && number_splits < number_difference) {
              Node<Scalar, N_NEIGHBOURS> second_new_node;
              second_new_node.ray = unit_vector(
                std::sin(phi_next), std::cos(phi_next), theta_offset + one * relative_index_next * theta_next);

              second_new_node.neighbours[LEFT] =
                end + (((relative_index_next + one) % number_points_next + number_points_next) % number_points_next);
              second_new_node.neighbours[RIGHT] =
                end + (((relative_index_next - one) % number_points_next + number_points_next) % number_points_next);
              second_new_node.neighbours[BELOWRIGHT] = indices[it];
              second_new_node.neighbours[BELOWLEFT]  = indices[(it + 1) % indices.size()];

              nodes[nodes.size() - 1].neighbours[BELOWRIGHT] =
                indices[((it - 1) % number_points_now + number_points_now) % number_points_now];
              nodes[nodes.size() - 1].neighbours[BELOWLEFT] = indices[it];

              nodes[indices[it]].neighbours[TOPLEFT]  = end + ((relative_index_next - 1) % number_points_next);
              nodes[indices[it]].neighbours[TOPRIGHT] = end + ((relative_index_next) % number_points_next);

              nodes.push_back(std::move(second_new_node));
              number_splits += 1;
              relative_index_next += 1;
            }
          }

          // *********************************************************************************

          relative_index_now += 1;
        }
        running_index = nodes.size();
      }

      // Join the last ring of points to one past the end
      for (unsigned int i = (nodes.size() - number_points.back()); i < nodes.size(); ++i) {
        nodes[i].neighbours[TOPLEFT]  = nodes.size();
        nodes[i].neighbours[TOPRIGHT] = nodes.size();
      }

      // // print out mesh points
      // for (size_t i = 0; i < nodes.size(); ++i) {
      //   std::cout << "meshpoint: " << i << ": " << nodes[i].neighbours[LEFT] << ", " << nodes[i].neighbours[TOPLEFT]
      //             << ", " << nodes[i].neighbours[TOPRIGHT] << ", " << nodes[i].neighbours[RIGHT] << ", "
      //             << nodes[i].neighbours[BELOWRIGHT] << "," << nodes[i].neighbours[BELOWLEFT] << std::endl;
      // }


      return nodes;
    }
  };

}  // namespace model
}  // namespace visualmesh

#endif  // VISUALMESH_MODEL_RADIAL6_HPP