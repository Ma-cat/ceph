// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (C) 2013 Cloudwatt <libre.licensing@cloudwatt.com>
 *
 * Author: Loic Dachary <loic@dachary.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 * 
 */

#include <errno.h>
#include "common/debug.h"
#include "ErasureCodeJerasure.h"
extern "C" {
#include "jerasure.h"
#include "reed_sol.h"
#include "galois.h"
#include "cauchy.h"
#include "liberation.h"
}

#define dout_subsys ceph_subsys_osd
#undef dout_prefix
#define dout_prefix _prefix(_dout)

static ostream& _prefix(std::ostream* _dout)
{
  return *_dout << "ErasureCodeJerasure: ";
}

void ErasureCodeJerasure::init(const map<std::string,std::string> &parameters) {
  dout(10) << "technique=" << technique << dendl;
  parse(parameters);
  prepare();
}

int ErasureCodeJerasure::minimum_to_decode(const set<int> &want_to_read,
                                           const set<int> &available_chunks,
                                           set<int> *minimum) {
  if (available_chunks.size() < (unsigned)k)
    return -EIO;
  set<int>::iterator i;
  unsigned j;
  for (i = available_chunks.begin(), j = 0; j < (unsigned)k; i++, j++)
    minimum->insert(*i);
  return 0;
}

int ErasureCodeJerasure::minimum_to_decode_with_cost(const set<int> &want_to_read,
                                                     const map<int, int> &available,
                                                     set<int> *minimum) {
  set <int> available_chunks;
  for (map<int, int>::const_iterator i = available.begin();
       i != available.end();
       i++)
    available_chunks.insert(i->first);
  return minimum_to_decode(want_to_read, available_chunks, minimum);
}

int ErasureCodeJerasure::encode(const set<int> &want_to_encode,
                                const bufferlist &in,
                                map<int, bufferlist> *encoded) {
  unsigned in_length = pad_in_length(in.length());
  dout(10) << "encode adjusted buffer length from " << in.length() << " to " << in_length << dendl;
  assert(in_length % k == 0);
  unsigned blocksize = in_length / k;
  unsigned length = blocksize * ( k + m );
  bufferlist out(in);
  bufferptr pad(length - in.length());
  pad.zero(0, k);
  out.push_back(pad);
  char *p = out.c_str();
  char *data[k];
  for (int i = 0; i < k; i++) {
    data[i] = p + i * blocksize;
  }
  char *coding[m];
  for (int i = 0; i < m; i++) {
    coding[i] = p + ( k + i ) * blocksize;
  }
  jerasure_encode(data, coding, blocksize);
  const bufferptr ptr = out.buffers().front();
  for (set<int>::iterator j = want_to_encode.begin();
       j != want_to_encode.end();
       j++) {
    bufferptr chunk(ptr, (*j) * blocksize, blocksize);
    (*encoded)[*j].push_front(chunk);
  }
  return 0;
}

int ErasureCodeJerasure::decode(const set<int> &want_to_read,
                                const map<int, bufferlist> &chunks,
                                map<int, bufferlist> *decoded) {
  unsigned blocksize = (*chunks.begin()).second.length();
  int erasures[k + m + 1];
  int erasures_count = 0;
  char *data[k];
  char *coding[m];
  for (int i =  0; i < k + m; i++) {
    if (chunks.find(i) == chunks.end()) {
      erasures[erasures_count] = i;
      erasures_count++;
      bufferptr ptr(blocksize);
      (*decoded)[i].push_front(ptr);
    } else {
      (*decoded)[i] = chunks.find(i)->second;
    }
    if (i < k)
      data[i] = (*decoded)[i].c_str();
    else
      coding[i - k] = (*decoded)[i].c_str();
  }
  erasures[erasures_count] = -1;

  if (erasures_count > 0)
    return jerasure_decode(erasures, data, coding, blocksize);
  else
    return 0;
}

int ErasureCodeJerasure::to_int(const std::string &name,
                                const map<std::string,std::string> &parameters,
                                int default_value) {
  if (parameters.find(name) == parameters.end() ||
      parameters.find(name)->second.size() == 0) {
    dout(10) << name << " defaults to " << default_value << dendl;
    return default_value;
  }
  const std::string value = parameters.find(name)->second;
  std::string p = value;
  std::string err;
  int r = strict_strtol(p.c_str(), 10, &err);
  if (!err.empty()) {
    derr << "could not convert " << name << "=" << value
         << " to int because " << err
         << ", set to default " << default_value << dendl;
    return default_value;
  }
  dout(10) << name << " set to " << r << dendl;
  return r;
}

bool ErasureCodeJerasure::is_prime(int value) {
  int prime55[] = {
    2,3,5,7,11,13,17,19,23,29,31,37,41,43,47,53,59,61,67,71,
    73,79,83,89,97,101,103,107,109,113,127,131,137,139,149,
    151,157,163,167,173,179,
    181,191,193,197,199,211,223,227,229,233,239,241,251,257
  };
  int i;
  for (i = 0; i < 55; i++)
    if (value == prime55[i])
      return true;
  return false;
}

// 
// ErasureCodeJerasureReedSolomonVandermonde
//
void ErasureCodeJerasureReedSolomonVandermonde::jerasure_encode(char **data,
                                                                char **coding,
                                                                int blocksize) {
  jerasure_matrix_encode(k, m, w, matrix, data, coding, blocksize);
}

int ErasureCodeJerasureReedSolomonVandermonde::jerasure_decode(int *erasures,
                                                                char **data,
                                                                char **coding,
                                                                int blocksize) {
  return jerasure_matrix_decode(k, m, w, matrix, 1, erasures, data, coding, blocksize);
}

unsigned ErasureCodeJerasureReedSolomonVandermonde::pad_in_length(unsigned in_length) {
  while (in_length%(k*w*sizeof(int)) != 0) 
    in_length++;
  return in_length;
}

void ErasureCodeJerasureReedSolomonVandermonde::parse(const map<std::string,std::string> &parameters) {
  k = to_int("erasure-code-k", parameters, DEFAULT_K);
  m = to_int("erasure-code-m", parameters, DEFAULT_M);
  w = to_int("erasure-code-w", parameters, DEFAULT_W);
  if (w != 8 && w != 16 && w != 32) {
    derr << "ReedSolomonVandermonde: w=" << w << " must be one of {8, 16, 32} : revert to 8 " << dendl;
    w = 8;
  }
}

void ErasureCodeJerasureReedSolomonVandermonde::prepare() {
  matrix = reed_sol_vandermonde_coding_matrix(k, m, w);
}

