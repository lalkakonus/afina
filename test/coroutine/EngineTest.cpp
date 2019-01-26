#include "gtest/gtest.h"

#include <iostream>
#include <sstream>
#include <stdio.h>

#include <afina/coroutine/Engine.h>

void _calculator_add(int &result, int left, int right) { result = left + right; }

TEST(CoroutineTest, SimpleStart) {
    Afina::Coroutine::Engine engine;

    int result;
    engine.start(_calculator_add, result, 1, 2);

    ASSERT_EQ(3, result);
}

void printa(Afina::Coroutine::Engine &pe, std::stringstream &out, void *&other) {
    out << "A1 ";
    std::cout << "A1 " << std::endl;
    pe.sched(other);

    out << "A2 ";
    std::cout << "A2 " << std::endl;
    pe.sched(other);

    out << "A3 ";
    std::cout << "A3 " << std::endl;
    pe.sched(other);
}

void printb(Afina::Coroutine::Engine &pe, std::stringstream &out, void *&other) {
    out << "B1 ";
    std::cout << "B1 " << std::endl;
    pe.sched(other);

    out << "B2 ";
    std::cout << "B2 " << std::endl;
    pe.sched(other);

    out << "B3 ";
    std::cout << "B3 " << std::endl;
}

std::stringstream out;
void *pa = nullptr, *pb = nullptr;
void _printer(Afina::Coroutine::Engine &pe, std::string &result) {
    // Create routines, note it doens't get control yet
    pa = pe.run(printa, pe, out, pb);
    pb = pe.run(printb, pe, out, pa);

    // Pass control to first routine, it will ping pong
    // between printa/printb greedely then we will get
    // control back
    pe.sched(pa);

    out << "END";
    std::cout << "END" << std::endl;

    // done
    result = out.str();
}

TEST(CoroutineTest, Printer) {
    Afina::Coroutine::Engine engine;

    std::string result;
    engine.start(_printer, engine, result);
    ASSERT_STREQ("A1 B1 A2 B2 A3 B3 END", result.c_str());
}


void coro1(Afina::Coroutine::Engine &pe, std::stringstream &out) {
    int a = 1, b = 1;
    for (int i = 0; i < 10; i++) {
        auto c = a + b;
        a = b;
        b = c;
    }
    out << "coro1 " << a << " " << b;
    std::cout << "coro1 " << a << " " << b << std::endl;
    pe.yield();
    out << "coro1 end " << a << " " << b;;
    std::cout << "coro1 end " << a << " " << b << std::endl; 
}


void yieldStarter(Afina::Coroutine::Engine &pe, std::stringstream &out) {
    double eps = 0.01;
    auto c1 = pe.run(coro1, pe, out);

    auto epsSquare = eps * eps;
    out << "starter " << epsSquare;
    std::cout << "starter " << epsSquare << std::endl;
    pe.yield();
    out << "starter end " << epsSquare;
    std::cout << "starter End " << epsSquare << std::endl;
}

TEST(CoroutineTest, YieldTest) {
    Afina::Coroutine::Engine engine;

    std::stringstream result;
    engine.start(yieldStarter, engine, result);
    ASSERT_STREQ("starter 0.0001coro1 89 144starter end 0.0001coro1 end 89 144", result.str().c_str());
}

