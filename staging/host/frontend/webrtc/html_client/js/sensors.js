/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

'use strict';

const g = 9.80665; // meter per second^2
const gravityVec = [0, g, 0];
const magnetic_field = [0, 5.9, -48.4];

function toRadians(x) {
  return x * Math.PI / 180;
}

function determinantOfMatrix(M) {
  // Only compute results for square matrices.
  if (M.length != M[0].length) {
    return 0;
  }
  if (M.length == 2) {
    return M[0][0] * M[1][1] - M[1][0] * M[0][1];
  }
  let result = 0.0;
  for (let i = 0; i < M.length; i++) {
    let factor = M[0][i] * (i % 2? -1 : 1);
    let subM = getSubmatrix(M, 0, i);
    result += factor * determinantOfMatrix(subM);
  }
  return result;
}

// Get submatrix that excludes row i, column j.
function getSubmatrix(M, i, j) {
  let subM = [];
  for (let k = 0; k < M.length; k++) {
    if (k == i) {
      continue;
    }
    let tmp = [];
    for (let l = 0; l < M.length; l++) {
      if (l == j) {
        continue;
      }
      tmp.push(M[k][l]);
    }
    subM.push(tmp);
  }
  return subM;
}

function invertMatrix(M) {
  // M ^ -1 = adj(M) / det(M)
  // adj(M) = transpose(cofactor(M))
  // Cij = (-1) ^ (i+j) det (Mij)
  let det = determinantOfMatrix(M);
  // If matrix is not invertible, return an empty matrix.
  if (det == 0) {
    return [[]];
  }
  let invM = [];
  for (let i = 0; i < M.length; i++) {
    let tmp = [];
    for (let j = 0; j < M.length; j++) {
      tmp.push(determinantOfMatrix(getSubmatrix(M, i,j)) * Math.pow(-1, i + j) / det);
    }
    invM.push(tmp);
  }
  invM = transposeMatrix(invM);
  return invM;
}

function transposeMatrix(M) {
  let transposedM = [];
  for (let j = 0; j < M.at(0).length; j++) {
    let tmp = [];
    for (let i = 0; i < M.length; i++) {
        tmp.push(M[i][j]);
    }
    transposedM.push(tmp);
  }
  return transposedM;
}

function matrixDotProduct(MA, MB) {
  // If given matrices are not valid for multiplication,
  // return an empty matrix.
  if (MA[0].length != MB.length) {
    return [[]];
  }

  let vec = [];
  for (let r = 0; r < MA.length; r++) {
    let tmp = [];
    for (let c = 0; c < MB[0].length; c++) {
      let dot = 0.0;
      for (let i = 0; i < MA[0].length; i++) {
        dot += MA[r][i] * MB[i][c];
      }
      tmp.push(dot);
    }
    vec.push(tmp);
  }
  return vec;
}

// Calculate the rotation matrix of the pitch, yaw, and roll angles.
function getRotationMatrix(xR, yR, zR) {
  xR = toRadians(-xR);
  yR = toRadians(-yR);
  zR = toRadians(-zR);
  let rz = [[Math.cos(zR), -Math.sin(zR), 0],
            [Math.sin(zR), Math.cos(zR), 0],
            [0, 0, 1]];
  let ry = [[Math.cos(yR), 0, Math.sin(yR)],
            [0, 1, 0],
            [-Math.sin(yR), 0, Math.cos(yR)]];
  let rx = [[1, 0, 0],
            [0, Math.cos(xR), -Math.sin(xR)],
            [0, Math.sin(xR), Math.cos(xR)]];
  let vec = matrixDotProduct(ry, rx);
  vec = matrixDotProduct(rz, vec);
  return vec;
}

// Calculate new Accelerometer values of the new rotation degrees.
function calculateAcceleration(rotation) {
  let rotationM = getRotationMatrix(rotation[0], rotation[1], rotation[2]);
  let acc = transposeMatrix(matrixDotProduct(rotationM, transposeMatrix([gravityVec])))[0];
  return acc.map((x) => x.toFixed(3));
}

// Calculate new Magnetometer values of the new rotation degrees.
function calculateMagnetometer(rotation) {
  let rotationM = getRotationMatrix(rotation[0], rotation[1], rotation[2]);
  let mgn = transposeMatrix(matrixDotProduct(rotationM, transposeMatrix([magnetic_field])))[0];
  return mgn.map((x) => x.toFixed(3));
}

// Convert rotation matrix to angular velocity numerator.
function getAngularRotation(m) {
  let trace = 0;
  for (let i = 0; i < m.length; i++) {
    trace += m[i][i];
  }
  let angle = Math.acos((trace - 1) / 2.0);
  if (angle == 0) {
    return [0, 0, 0];
  }
  let factor = 1.0 / (2 * Math.sin(angle));
  let axis = [m[2][1] - m[1][2],
              m[0][2] - m[2][0],
              m[1][0] - m[0][1]];
  // Get angular velocity numerator
  return axis.map((x) => x * factor * angle);
}

// Calculate new Gyroscope values relative to the new rotation degrees.
function calculateGyroscope(rotationA, rotationB, time_dif) {
  let priorRotationM = getRotationMatrix(rotationA[0], rotationA[1], rotationA[2]);
  let currentRotationM = getRotationMatrix(rotationB[0], rotationB[1], rotationB[2]);
  let transitionMatrix = matrixDotProduct(priorRotationM, invertMatrix(currentRotationM));

  const gyro = getAngularRotation(transitionMatrix);
  return gyro.map((x) => (x / time_dif).toFixed(3));
}
