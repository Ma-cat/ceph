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

#ifndef CEPH_ERASURE_CODE_JERASURE_H
#define CEPH_ERASURE_CODE_JERASURE_H

#include "osd/ErasureCodeInterface.h"

class ErasureCodeJerasure : public ErasureCodeInterface {
public:
  int k;
  int m;
  int w;
  const char *technique;

  ErasureCodeJerasure(const char *_technique) :
    technique(_technique)
  {}

  virtual ~ErasureCodeJerasure() {}
  
  virtual int minimum_to_decode(const set<int> &want_to_read,
                                const set<int> &available_chunks,
                                set<int> *minimum);

  virtual int minimum_to_decode_with_cost(const set<int> &want_to_read,
                                          const map<int, int> &available,
                                          set<int> *minimum);

  virtual int encode(const set<int> &want_to_encode,
                     const bufferlist &in,
                     map<int, bufferlist> *encoded);

  virtual int decode(const set<int> &want_to_read,
                     const map<int, bufferlist> &chunks,
                     map<int, bufferlist> *decoded);

  void init(const map<std::string,std::string> &parameters);
  virtual void jerasure_encode(char **data,
                               char **coding,
                               int blocksize) = 0;
  virtual int jerasure_decode(int *erasures,
                               char **data,
                               char **coding,
                               int blocksize) = 0;
  virtual unsigned pad_in_length(unsigned in_length) = 0;
  virtual void parse(const map<std::string,std::string> &parameters) = 0;
  virtual void prepare() = 0;
  static int to_int(const std::string &name,
                    const map<std::string,std::string> &parameters,
                    int default_value);
  static bool is_prime(int value);
};

class ErasureCodeJerasureReedSolomonVandermonde : public ErasureCodeJerasure {
public:
  static const int DEFAULT_K = 7;
  static const int DEFAULT_M = 3;
  static const int DEFAULT_W = 8;
  int *matrix;

  ErasureCodeJerasureReedSolomonVandermonde() :
    ErasureCodeJerasure("reed_sol_van"),
    matrix(0)
  { }
  virtual ~ErasureCodeJerasureReedSolomonVandermonde() {
    if (matrix)
      free(matrix);
  }

  virtual void jerasure_encode(char **data,
                               char **coding,
                               int blocksize);
  virtual int jerasure_decode(int *erasures,
                               char **data,
                               char **coding,
                               int blocksize);
  virtual unsigned pad_in_length(unsigned in_length);
  virtual void parse(const map<std::string,std::string> &parameters);
  virtual void prepare();
};
#endif
