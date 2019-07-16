#!/usr/bin/env python3

scene_template = """
points
  file: {{ filename }}
  frame: 0
  mat: 0

model
  file: ground.obj
  scale: 1.0
  offset: <0,0,0>
  mat: 1

render
  width: 800
  height: 600
  samples: 32
  backclr: <.1, .2, .4>
  envmap: sky.png
  outfile: img%04d.png

volume
  scale: 2.0
  steps: <.25, 16, .25>
  extinct: <-1, 1.1, 0.0>
  range: <0, -1, 3>
  cutoff: <0.005, 0.001, 0.0>
  smooth: 1
  smoothp: <1.0, -0.2, 0.0>

camera
  angs: <5, 20, 0>
  target: <30, 10, 30>
  dist: 200
  fov: 50

light
  angs: <-186, 128, 0>
  target: <1125, 110, 1110>
  dist: 2000

material
  lightwid: 0.9
  shwid: 0.1
  shbias: 0.5
  ambient: <0.1, 0.15, 0.15>
  diffuse: <0.1, 0.12, 0.18>
  spec: <1.2, 1.2, 1.2>
  spow: 200
  env: <0,0,0>
  reflwid: 0.2
  reflbias: 0.5
  reflcolor: <0.3, 0.3, 0.3>
  refrwid: 0.05
  refrcolor: <0.2, 0.2, 0.25>
  refrior: 1.33
  reframt: 0.8
  refroffs: 100
  refrbias: 0.5

material
  lightwid: 0.9
  shwid: 0.1
  shbias: 0.5
  ambient: <0.1, 0.1, 0.1>
  diffuse: <1.0, 1.0, 1.0>
  spec: <0.0, 0.0, 0.0>
  spow: 20
  env: <0,0,0>
  reflwid: 0.0
  reflbias: 0.5
  reflcolor: <0.5, 0.5, 0.5>
  refrwid: 0.0
"""


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


def generate_particles_box_strided(
    material_density,  # in kg/m3
    origin=[0.0, 0.0, 0.0],  # in cm
    particles_per_cell=None, n_particles=None, size=None  # specify two out of three parameters
):
    particle_positions = []

    if particles_per_cell is None:
        volume = size[0] * size[1] * size[2]
        particles_per_cell = n_particles / volume
    elif n_particles is None:
        volume = size[0] * size[1] * size[2]
        n_particles = int(volume * particles_per_cell)
    else:  # size is None
        volume = n_particles / particles_per_cell
        size = [volume ** (1/3)] * 3

    volume = size[0] * size[1] * size[2]
    particle_volume = volume / n_particles
    particle_size = particle_volume ** (1/3)
    stride = [particle_size] * 3

    for pos_x in rangef(origin[0], origin[0] + size[0], stride[0]):
        for pos_y in rangef(origin[1], origin[1] + size[1], stride[1]):
            for pos_z in rangef(origin[2], origin[2] + size[2], stride[2]):
                particle_positions.append([pos_x, pos_y, pos_z])

    return {
        'particle_positions': particle_positions,  # in cm
        'particle_volume': particle_volume * 1e-6,  # convert cm3 to m3
        'particle_per_cell': particles_per_cell,
        'particle_mass': material_density * particle_volume * 1e-6,  # in kg
        'size': size,  # in cm
        'origin': origin,  # in cm
    }


def print_particle_data(title, particle_data):
    print()
    print(title)
    print('  Particle volume (m^3) :', particle_data['particle_volume'])
    print('  Particles per cell    :', particle_data['particle_per_cell'])
    print('  Particle mass (kg)    :', particle_data['particle_mass'])
    print('  Particle count        :', len(particle_data['particle_positions']))
    print('  Box size (cm)         :', particle_data['size'])
    print('  Origin (cm)           :', particle_data['origin'])
    print()


def write_particles(title, particle_data):
    with open(title + '.dat', 'w') as fout:
        fout.write(str(len(particle_data['particle_positions'])) + '\n')
        fout.write(str(particle_data['particle_volume']) + '\n')
        fout.write(str(particle_data['particle_mass']) + '\n')

        for i in range(len(particle_data['particle_positions'])):
            fout.write(str(particle_data['particle_positions'][i][0]) + ' ')
            fout.write(str(particle_data['particle_positions'][i][1]) + ' ')
            fout.write(str(particle_data['particle_positions'][i][2]) + '\n')

    with open(title + '.scn', 'w') as fout:
        fout.write(scene_template.replace('{{ filename }}', title + '.dat'))

    print_particle_data(title, particle_data)


if __name__ == '__main__':

    write_particles('small', generate_particles_box_strided(
        material_density=1000.0, origin=[20.0, 20.0, 20.0],
        particles_per_cell=8, size=[20.0, 20.0, 20.0]
    ))

    # Fixed size, variable PPC and particle count
    for ppc in [4, 8, 16]:
        write_particles('fixedsize-ppc%d' % ppc, generate_particles_box_strided(
            material_density=1000.0, origin=[20.0, 20.0, 20.0],
            particles_per_cell=ppc, size=[50.0, 50.0, 50.0]
        ))

    # Fixed PPC, variable particle count and size
    for n in [400000, 800000, 1600000]:
        write_particles('fixedppc-n%dk' % int(n/1000), generate_particles_box_strided(
            material_density=1000.0, origin=[20.0, 20.0, 20.0],
            n_particles=n, particles_per_cell=8
        ))

    # Fixed particle count, variable PPC and size
    for ppc in [4, 8, 16]:
        write_particles('fixedn-ppc%d' % ppc, generate_particles_box_strided(
            material_density=1000.0, origin=[20.0, 20.0, 20.0],
            particles_per_cell=ppc, n_particles=1000000
        ))
