====================
OVN hmap experiments
====================

This repository is a standalone artifact for OVN_/OVS_ hmap performance
experiments. It starts from OVN 26.03 and vendors the matching OVS source tree
under ``ovs/`` so experiment branches can contain both OVN and OVS changes in
one repository.

This is not an upstream OVN development fork. It exists to preserve experiment
code, benchmark commands, and results for the accompanying hashmap performance
writeup.

Benchmark scripts
=================

Run these commands from the repository root after configuring and building OVN.

The primary workload is OVN's built-in 200x200 northd scale test: 200
hypervisors with 200 logical ports per hypervisor.

Run the end-to-end benchmark:

::

    ./bench/run-check-perf-200x200.sh

Run the same workload under ``perf`` and generate an hmap-focused report:

::

    ./bench/profile-hmap-200x200.sh

Compare two saved ``results.txt`` files:

::

    ./bench/compare-check-perf.py baseline-results.txt experiment-results.txt

Results are written under ``results/``.

.. _OVN: https://www.ovn.org/en/
.. _OVS: https://www.openvswitch.org/
