#!/usr/bin/env python3

from points import read_points, write_points

if __name__ == '__main__':

    points = []
    raw_points = []
    bounding_box_min = tuple()
    bounding_box_max = tuple()
    bb_min_actual = [999999, 999999, 999999]
    bb_max_actual = [-999999, -999999, -999999]

    raw_points, bounding_box_min, bounding_box_max = read_points('points2.dat')

    for i in range(len(raw_points)):
        x, y, z = raw_points[i]
        if x != 0 and y != 0 and z != 0:
            points.append((x, y, z))

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

    bounding_box_min = (bounding_box_min[0], bounding_box_min[1], bounding_box_min[2] + 200)
    bounding_box_max = (bounding_box_max[0], bounding_box_max[1], bounding_box_max[2] + 200)

    write_points('points3.dat', points, bounding_box_min, bounding_box_max)
    print('Write completed.')
