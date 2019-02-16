import struct

def read_points(filename):
    points = []

    with open(filename, 'rb') as fin:
        header = struct.unpack('Iffffff', fin.read(28))
        n_points = header[0] # ignored, as it can potentially be incorrect
        bounding_box_min = list(header[1:4])
        bounding_box_max = list(header[4:7])

        cc = 0
        point_bytes = fin.read(6)
        while point_bytes:
            points.append(struct.unpack('HHH', point_bytes))
            point_bytes = fin.read(6)

    return (points, bounding_box_min, bounding_box_max)


def write_points(filename, points, bounding_box_min, bounding_box_max):
    with open(filename, 'wb') as fout:
        fout.write(struct.pack('I', len(points)))
        fout.write(struct.pack('fff', *bounding_box_min))
        fout.write(struct.pack('fff', *bounding_box_max))

        for i in range(len(points)):
            fout.write(struct.pack('HHH', *points[i]))
