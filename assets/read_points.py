#!/usr/bin/env python3

from points import read_points, write_points

if __name__ == '__main__':

    points = []
    bounding_box_min = tuple()
    bounding_box_max = tuple()
    bb_min_actual = [999999, 999999, 999999]
    bb_max_actual = [-999999, -999999, -999999]

    print('Reading points...')
    points, bounding_box_min, bounding_box_max = read_points('points2.dat')
    print('Points loaded.')

    for i in range(len(points)):
        x, y, z = points[i]
        if (x < bb_min_actual[0]):
            bb_min_actual[0] = x
        if (y < bb_min_actual[1]):
            bb_min_actual[1] = y
        if (z < bb_min_actual[2]):
            bb_min_actual[2] = z
        if (x > bb_max_actual[0]):
            bb_max_actual[0] = x
        if (y > bb_max_actual[1]):
            bb_max_actual[1] = y
        if (z > bb_max_actual[2]):
            bb_max_actual[2] = z

    print('Non-zero point count:', len(points))
    print('Bounding box minimum:', bounding_box_min)
    print('Bounding box maximum:', bounding_box_max)
    print('bb_min_actual:', bb_min_actual)
    print('bb_max_actual:', bb_max_actual)
    print('---')

    for i in range(len(points)):
        x, y, z = points[i]
        print(x, y, z)
