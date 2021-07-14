#!/bin/bash

time /build/souffle/src/souffle \
  -F /data/test-1k/processed/python /souffle/test-queries/parquet-test.dl -D- \
> /souffle/test-queries/parquet-results.txt

time /build/souffle/src/souffle \
  -F /data/test-1k/processed/python /souffle/test-queries/csv-test.dl -D- \
> /souffle/test-queries/csv-results.txt
