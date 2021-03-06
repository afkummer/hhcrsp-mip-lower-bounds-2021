/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2020
 * Alberto Francisco Kummer Neto (afkneto@inf.ufrgs.br),
 * Luciana Salete Buriol (buriol@inf.ufrgs.br) and
 * Olinto César Bassi de Araújo (olinto@ctism.ufsm.br)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "Solution.h"

#include <cassert>
#include <fstream>
#include <iostream>

using namespace std;

Solution::Solution(const Instance& inst_): inst(inst_) {
   // Resizing of data structures
   routes.resize(inst.numVehicles());

   // All vehicles start at depot node...
   vehiPos.resize(inst.numVehicles(), 0);
   for (int v = 0; v < inst.numVehicles(); ++v) {
      routes[v].reserve(inst.numNodes());
      routes[v].emplace_back(make_tuple(0, 0));
   }

   // And are ready to leave at time 0.
   vehiLeaveTime.resize(inst.numVehicles(), 0.0);

   // Cached vehicles and service start times.
   svcVehi.resize(inst.numNodes(), make_tuple(-1, -1));
   svcStartTm.resize(inst.numNodes(), make_tuple(0.0, 0.0));

   // Initialize the solution cost indicators.
   dist = 0.0;
   tard = 0.0;
   tmax = 0.0;
   cachedCost = 0.0;
}

Solution &Solution::operator=(const Solution &other) {
   taskOrder = other.taskOrder;
   routes = other.routes;
   vehiPos = other.vehiPos;
   vehiLeaveTime = other.vehiLeaveTime;
   svcVehi = other.svcVehi;
   svcStartTm = other.svcStartTm;
   dist = other.dist;
   tard = other.tard;
   tmax = other.tmax;
   cachedCost = other.cachedCost;
   return *this;
}

double Solution::findInsertionCost(Task &task) const {
   assert(task.vehi[0] != -1 && "First vehicle is unset.");

   // Compute the arrival time of the first vehicle.
   double arrivalV0 = max(inst.nodeTwMin(task.node), vehiLeaveTime[task.vehi[0]] + inst.distance(vehiPos[task.vehi[0]], task.node));

   if (inst.nodeSvcType(task.node) == Instance::SvcType::SINGLE) {

      assert(task.skills[0] != -1 && "First skill for single service patient unset.");
      assert(task.skills[1] == -1 && "Second skill for single service patient set.");

      double tardinessV0 = max(0.0, arrivalV0 - inst.nodeTwMax(task.node));

      task.startTime[0] = arrivalV0;
      task.leaveTime[0] = arrivalV0 + inst.nodeProcTime(task.node, task.skills[0]);

      task.incDist = inst.distance(vehiPos[task.vehi[0]], task.node);
      task.incTard = tardinessV0;
      task.currTmax = tardinessV0;

   } else {
      assert(task.vehi[1] != -1 && "Second vehicle is unset.");

      // Computes the arrival time of the second vehicle.
      double arrivalV1 = max(inst.nodeTwMin(task.node), vehiLeaveTime[task.vehi[1]] + inst.distance(vehiPos[task.vehi[1]], task.node));

      if (inst.nodeSvcType(task.node) == Instance::SvcType::SIM) {

         assert(task.skills[0] != -1 && "First skill for simultaneous double service patient unset.");
         assert(task.skills[1] != -1 && "Second skill for simultaneous double service patient unset.");

         double startTime = max(arrivalV0, arrivalV1);

         double startTimeV0 = startTime;
         double startTimeV1 = startTime;

         double tardinessV0 = max(0.0, startTimeV0 - inst.nodeTwMax(task.node));
         double tardinessV1 = max(0.0, startTimeV1 - inst.nodeTwMax(task.node));

         task.startTime[0] = startTimeV0;
         task.startTime[1] = startTimeV1;
         task.leaveTime[0] = startTimeV0 + inst.nodeProcTime(task.node, task.skills[0]);
         task.leaveTime[1] = startTimeV1 + inst.nodeProcTime(task.node, task.skills[1]);

         task.incDist = inst.distance(vehiPos[task.vehi[0]], task.node) + inst.distance(vehiPos[task.vehi[1]], task.node);
         task.incTard = tardinessV0 + tardinessV1;
         task.currTmax = max(tardinessV0, tardinessV1);

      } else {

         assert(task.skills[0] != -1 && "First skill for double service precedence patient unset.");
         assert(task.skills[1] != -1 && "Second skill for double service precedence patient unset.");

         double startTimeV0 = arrivalV0;
         double startTimeV1 = max(arrivalV1, startTimeV0 + inst.nodeDeltaMin(task.node));

         // Fix any violation of maximum separation time.
         double violDeltaMax = max(0.0, (startTimeV1 - startTimeV0) - inst.nodeDeltaMax(task.node));
         startTimeV0 += violDeltaMax;

         assert((startTimeV1-startTimeV0)+0.5 >= inst.nodeDeltaMin(task.node) && "Violation delta_min on double service with precedence.");
         assert((startTimeV1-startTimeV0)-0.5 <= inst.nodeDeltaMax(task.node) && "Violation delta_max on double service with precedence.");

         double tardinessV0 = max(0.0, startTimeV0 - inst.nodeTwMax(task.node));
         double tardinessV1 = max(0.0, startTimeV1 - inst.nodeTwMax(task.node));

         task.startTime[0] = startTimeV0;
         task.startTime[1] = startTimeV1;
         task.leaveTime[0] = startTimeV0 + inst.nodeProcTime(task.node, task.skills[0]);
         task.leaveTime[1] = startTimeV1 + inst.nodeProcTime(task.node, task.skills[1]);

         task.incDist = inst.distance(vehiPos[task.vehi[0]], task.node) + inst.distance(vehiPos[task.vehi[1]], task.node);
         task.incTard = tardinessV0 + tardinessV1;
         task.currTmax = max(tardinessV0, tardinessV1);

      }
   }

   task.cachedCost =
      COEFS[0] * (dist + task.incDist) +
      COEFS[1] * (tard + task.incTard) +
      COEFS[2] * max(tmax, task.currTmax)
   ;

   return task.cachedCost;
}

void Solution::updateRoutes(const Task &task) {
   assert(task.skills[0] != -1 && "First skill for simultaneous double service patient unset.");
   assert(task.vehi[0] != -1 && "Vehicle for the first skill unset.");

   routes[task.vehi[0]].push_back(make_tuple(task.node, task.skills[0]));
   vehiPos[task.vehi[0]] = task.node;
   vehiLeaveTime[task.vehi[0]] = task.leaveTime[0];
   get<0>(svcVehi[task.node]) = task.vehi[0];
   get<0>(svcStartTm[task.node]) = task.startTime[0];

   if (task.skills[1] != -1) {
      assert(task.skills[1] != -1 && "Second skill for simultaneous double service patient unset.");
      assert(task.vehi[1] != -1 && "Vehicle for the second skill unset.");

      routes[task.vehi[1]].push_back(make_tuple(task.node, task.skills[1]));
      vehiPos[task.vehi[1]] = task.node;
      vehiLeaveTime[task.vehi[1]] = task.leaveTime[1];
      get<1>(svcVehi[task.node]) = task.vehi[1];
      get<1>(svcStartTm[task.node]) = task.startTime[1];
   }

   dist += task.incDist;
   tard += task.incTard;
   tmax = max(tmax, task.currTmax);

   cachedCost = task.cachedCost;
}

void Solution::finishRoutes() {
   // Add the depot node at the end of each vehicle route.
   for (int v = 0; v < inst.numVehicles(); ++v) {
      int lastNode = get<0>(routes[v].back());
      dist += inst.distance(lastNode, 0);
      routes[v].push_back(make_tuple(0, 0));
   }

   // Update the solution to take into account the distances on returning to depot.
   cachedCost =
      COEFS[0] * dist +
      COEFS[1] * tard +
      COEFS[2] * tmax;
}

void Solution::writeTxt(const char* fname) const {
   ofstream fid(fname);
   if (!fid) {
      cout << "Solution file '" << fname << "' can not be written." << endl;
      exit(EXIT_FAILURE);
   }

   fid << "# Solution for " << inst.fileName() << "\n";
   fid << "# Cost = " << cachedCost << " Dist = " << dist << " Tard = " <<
      tard << " TMax = " << tmax << "\n";
   fid << "# <vehicle> <route length>\n";
   fid << "# <origin node> <service type>\n";

   for (int v = 0; v < inst.numVehicles(); ++v) {
      fid << v << " " << routes[v].size() << "\n";
      for (unsigned pos = 0; pos < routes[v].size(); ++pos) {
         fid << get<0>(routes[v][pos]) << ' ';
         fid << get<1>(routes[v][pos]) << '\n';
      }
   }
}

void Solution::writeTxt2(const char* fname) const {
   ofstream fid(fname);
   if (!fid) {
      cout << "Solution file '" << fname << "' can not be written." << endl;
      exit(EXIT_FAILURE);
   }

   fid << "# Solution for " << inst.fileName() << "\n";
   fid << "# Cost = " << cachedCost << " Dist = " << dist << " Tard = " <<
      tard << " TMax = " << tmax << "\n";
   fid << "# <vehicle> <route length>\n";
   fid << "# <originx> <originy> <destx> <desty> <service type>\n";

   for (int v = 0; v < inst.numVehicles(); ++v) {
      fid << v << " " << routes[v].size()-1 << "\n";

      for (unsigned pos = 1; pos < routes[v].size(); ++pos) {
         int originNode = get<0>(routes[v][pos-1]);
         int destNode = get<0>(routes[v][pos]);
         int destSvc = get<1>(routes[v][pos]);

         fid << inst.nodePosX(originNode) << ' ' << inst.nodePosY(originNode) << ' ';
         fid << inst.nodePosX(destNode) << ' ' << inst.nodePosY(destNode) << ' ';
         fid << destSvc << endl;
      }
   }
}

Solution Solution::readFromFile(const Instance &inst, const string &fname) {
   // First read the routes from the text file.
   vector<Solution::Route> routes(inst.numVehicles());
   {
      ifstream f(fname);
      if (!f) {
         cout << "Solution file '" << fname << "' could not be open to read.\n";
         exit(EXIT_FAILURE);
      }

      // Discard the headers.
      string line;
      for (int i : {1, 2, 3, 4}) {
         (void)i;
         getline(f, line);
      }

      // Starts reading the routes.

      int vId, len;
      while (f >> vId >> len) {
         for (int k = 0; k < len; ++k) {
            int i, s;
            f >> i >> s;
            routes[vId].emplace_back(i, s);
         }
      }
   }

   // Next, assemble the solution object using the routes read.
   Solution sol(inst);
   {
      auto allTasks{createTaskList(inst)};

      // Prepares the structure that keeps the open routes.
      vector<int> vhead(inst.numVehicles(),
                        1);  // Expects the routes starting and finish at depot.

      vector<int> actFlag(inst.numVehicles(), 0);
      int nFinish = 0;

      while (nFinish < inst.numVehicles()) {
         vector<Task *> dsCache;

         for (int v = 0; v < inst.numVehicles(); ++v) {
            auto [i, s] = routes[v][vhead[v]];
            if (i == 0) {
               if (actFlag[v] == 0) {
                  ++nFinish;
                  actFlag[v] = 1;
               }
            } else {
               Task &t{allTasks[i - 1]};
               if (inst.nodeSvcType(i) == Instance::SvcType::SINGLE) {
                  t.vehi[0] = v;
                  sol.findInsertionCost(t);
                  sol.updateRoutes(t);
                  sol.taskOrder.push_back(t);
                  vhead[v]++;
               } else {
                  if (s == t.skills[0])
                     t.vehi[0] = v;
                  else
                     t.vehi[1] = v;
                  if (t.vehi[0] != -1 && t.vehi[1] != -1) {
                     sol.findInsertionCost(t);
                     sol.updateRoutes(t);
                     sol.taskOrder.push_back(t);
                     vhead[t.vehi[0]]++;
                     vhead[t.vehi[1]]++;
                  }
               }
            }
         }
      }

      sol.finishRoutes();
   }

   return sol;
}

bool operator==(const Solution::Route &r1, const Solution::Route &r2) {
   if (r1.size() != r2.size())
      return false;
   auto sz = r1.size();
   for (size_t i = 0; i < sz; ++i) {
      if (get<0>(r1[i]) != get<0>(r2[i]))
         return false;
      if (get<1>(r1[i]) != get<1>(r2[i]))
         return false;
   }
   return true;
}

