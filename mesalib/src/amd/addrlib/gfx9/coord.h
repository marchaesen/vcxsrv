/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

// Class used to define a coordinate bit

#ifndef __COORD_H
#define __COORD_H

class Coordinate
{
public:
    Coordinate();
    Coordinate(INT_8 c, INT_32 n);

    VOID set(INT_8 c, INT_32 n);
    UINT_32 ison(UINT_32 x, UINT_32 y, UINT_32 z = 0, UINT_32 s = 0, UINT_32 m = 0) const;
    INT_8   getdim();
    INT_8   getord();

    BOOL_32 operator==(const Coordinate& b);
    BOOL_32 operator<(const Coordinate& b);
    BOOL_32 operator>(const Coordinate& b);
    BOOL_32 operator<=(const Coordinate& b);
    BOOL_32 operator>=(const Coordinate& b);
    BOOL_32 operator!=(const Coordinate& b);
    Coordinate& operator++(INT_32);

private:
    INT_8 dim;
    INT_8 ord;
};

class CoordTerm
{
public:
    CoordTerm();
    VOID Clear();
    VOID add(Coordinate& co);
    VOID add(CoordTerm& cl);
    BOOL_32 remove(Coordinate& co);
    BOOL_32 Exists(Coordinate& co);
    VOID copyto(CoordTerm& cl);
    UINT_32 getsize();
    UINT_32 getxor(UINT_32 x, UINT_32 y, UINT_32 z = 0, UINT_32 s = 0, UINT_32 m = 0) const;

    VOID getsmallest(Coordinate& co);
    UINT_32 Filter(INT_8 f, Coordinate& co, UINT_32 start = 0, INT_8 axis = '\0');
    Coordinate& operator[](UINT_32 i);
    BOOL_32 operator==(const CoordTerm& b);
    BOOL_32 operator!=(const CoordTerm& b);
    BOOL_32 exceedRange(UINT_32 xRange, UINT_32 yRange = 0, UINT_32 zRange = 0, UINT_32 sRange = 0);

private:
    static const UINT_32 MaxCoords = 8;
    UINT_32 num_coords;
    Coordinate m_coord[MaxCoords];
};

class CoordEq
{
public:
    CoordEq();
    VOID remove(Coordinate& co);
    BOOL_32 Exists(Coordinate& co);
    VOID resize(UINT_32 n);
    UINT_32 getsize();
    virtual UINT_64 solve(UINT_32 x, UINT_32 y, UINT_32 z = 0, UINT_32 s = 0, UINT_32 m = 0) const;
    virtual VOID solveAddr(UINT_64 addr, UINT_32 sliceInM,
                           UINT_32& x, UINT_32& y, UINT_32& z, UINT_32& s, UINT_32& m) const;

    VOID copy(CoordEq& o, UINT_32 start = 0, UINT_32 num = 0xFFFFFFFF);
    VOID reverse(UINT_32 start = 0, UINT_32 num = 0xFFFFFFFF);
    VOID xorin(CoordEq& x, UINT_32 start = 0);
    UINT_32 Filter(INT_8 f, Coordinate& co, UINT_32 start = 0, INT_8 axis = '\0');
    VOID shift(INT_32 amount, INT_32 start = 0);
    virtual CoordTerm& operator[](UINT_32 i);
    VOID mort2d(Coordinate& c0, Coordinate& c1, UINT_32 start = 0, UINT_32 end = 0);
    VOID mort3d(Coordinate& c0, Coordinate& c1, Coordinate& c2, UINT_32 start = 0, UINT_32 end = 0);

    BOOL_32 operator==(const CoordEq& b);
    BOOL_32 operator!=(const CoordEq& b);

private:
    static const UINT_32 MaxEqBits = 64;
    UINT_32 m_numBits;

    CoordTerm m_eq[MaxEqBits];
};

#endif

