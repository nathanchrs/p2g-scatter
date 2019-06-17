#!/usr/bin/env python3


def rangef(start, stop, step):
    i = start
    if (start < stop):
        while i < stop:
            yield i
            i += step
    else:
        while i > stop:
            yield i
            i -= step


def generate_particles_box_uniform(origin, size, stride):
    particle_positions = []

    for pos_x in rangef(origin[0], origin[0] + size[0], stride[0]):
        for pos_y in rangef(origin[1], origin[1] + size[1], stride[1]):
            for pos_z in rangef(origin[2], origin[2] + size[2], stride[2]):
                particle_positions.append([pos_x, pos_y, pos_z])
    return particle_positions


def write_particles(filename, particle_positions, particle_initial_volume, particle_mass):
    with open(filename, 'w') as fout:
        fout.write(str(len(particle_positions)) + '\n')
        fout.write(str(particle_initial_volume) + '\n')
        fout.write(str(particle_mass) + '\n')

        for i in range(len(particle_positions)):
            fout.write(str(particle_positions[i][0]) + ' ')
            fout.write(str(particle_positions[i][1]) + ' ')
            fout.write(str(particle_positions[i][2]) + '\n')

if __name__ == '__main__':
    points = generate_particles_box_uniform(
        origin=[20.0, 20.0, 20.0],
        size=[20.0, 20.0, 20.0],
        stride=[0.5, 0.5, 0.5]
    )

    print('Number of particles: %d' % len(points))

    write_particles(
        filename='points_new.dat',
        particle_positions=points,
        particle_initial_volume=1.25e-7,
        particle_mass=1.25e-4
    )
