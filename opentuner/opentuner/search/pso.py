# -*- coding: utf-8 -*-
from opentuner.search import technique, manipulator
import random

N=10

class PSO(technique.SequentialSearchTechnique ):
    """ Particle Swarm Optimization """
    def __init__(self,*pargs, **kwargs):
        super(PSO, self).__init__(*pargs, **kwargs)
    
    def main_generator(self):
        
        objective   = self.objective
        driver      = self.driver
        m = PSOmanipulator(self.manipulator.params)
        def config(cfg):
            return driver.get_configuration(cfg)

        population = [ParticleII(m) for i in range(N)]
        for p in population:
##            print p.position
            yield driver.get_configuration(p.position)
            
        while True:
            # For each particle
            for particle in population:
                g = driver.best_result.configuration.data
##                print "GLOBAL", g
##                print "MOVE FROM", particle.position
                particle.move(g)
##                print "MOVE TO", particle.position

##                print particle.velocity
                
                # send out for measurement
                yield config(particle.position)
                # update individual best
                if objective.lt(config(particle.position), config(particle.best)):
                    particle.best = particle.position
##                    print "UPDATE:", particle.position
                    # update global best: done automatically by search driver?
                    # TODO: swarm best == global best?
##                print "POPULATION:"
##                for p in population:
##                    print p.position 
                            
 


class Particle(object):     # should inherit from/link to ConfigurationManipulator? 
    def __init__(self, m, omega=1, phi_l=0.5, phi_g=0.5):
        """
        m: a configuraiton manipulator
        omega: influence of the particle's last velocity, a float in range [0,1] ; omega=1 means even speed
        phi_l: influence of the particle's distance to its historial best position, a float in range [0,1] 
        phi_g: influence of the particle's distance to the global best position, a float in range [0,1]
        """
        
        self.manipulator = m
        self.velocity = m.difference(m.random(), m.random())   # velocity domain; initial value
        self.position = self.manipulator.random()   # whatever cfg is...
        self.best = self.position
        self.omega = omega
        self.phi_l = phi_l
        self.phi_g = phi_g
        
    def __str__(self):
        return 'V:'+str(self.velocity)+'\tP:'+str(self.position)

    def move(self, global_best):
        """ move the particle towards its historical best and global best """
        m = self.manipulator
        v = m.sum_v(
            m.scale(self.velocity,random.uniform(0,self.omega)),
            m.scale(m.difference(self.best, self.position),random.uniform(0,self.phi_l)),
            m.scale(m.difference(global_best, self.position),random.uniform(0,self.phi_g))
                  )
        self.velocity = v
        self.position = m.add_v(self.position, v)


class ParticleII(Particle):
    def __init__(self, m, omega=0.5, phi=0.5):
        super(ParticleII, self).__init__(m, omega, phi, phi)        

    def move(self, global_best):
        m = self.manipulator
        # Decide if crossover happens
        if random.uniform(0,1)<self.omega:
            return
        else:
            if random.uniform(0,1)<self.phi_l:
                o = m.crossover(self.position, global_best)
            else:
                o = m.crossover(self.position, self.best)
            self.position = o
        


class ParticleIII(Particle):
    """
    At each step, randomly chooses one motion out of:
    (i) continuing previous motion
    (ii) moving towards local best
    (iii) moving towards global best
    """

    def move(self, global_best):
        m = self.manipulator
        # Randomly choose one direction instead of combining all three
        vs= [
            m.scale(self.velocity,random.uniform(0,self.omega)),
            m.scale(m.difference(self.best, self.position),random.uniform(0,self.phi_l)),
            m.scale(m.difference(global_best, self.position),random.uniform(0,self.phi_g))
            ]
                
        choice = random.randint(0,2)
        self.velocity = vs[choice]
        self.position = m.add_v(self.position, vs[choice])
        
        
class ParticleIV(Particle):
    """
    Similar to ParticleIII except that velocity can only be a subsequence of the swap sequence
    velocity, and the unused portion of the swap sequence is stored as the particle's velocity
    """
    def move(self, global_best):
        m = self.manipulator
        # Randomly choose one direction instead of combining all three
        vs= [
            m.split(self.velocity,random.uniform(0,self.omega)),
            m.split(m.difference(self.best, self.position),random.uniform(0,self.phi_l)),
            m.split(m.difference(global_best, self.position),random.uniform(0,self.phi_g))
            ]
                
        choice = random.randint(0,2)
        self.velocity = vs[choice][1]
        self.position = m.add_v(self.position, vs[choice][0])
        
    
class PSOmanipulator(manipulator.ConfigurationManipulator):
    def difference(self, cfg1, cfg2):
        """ Return the difference of two positions i.e. velocity """
        v = {}
        for p in self.params:
            if isinstance(p, manipulator.PermutationParameter):
                v[p.name]=p.swap_dist(cfg1, cfg2)       # no longer legal configuration
            else:
                p.difference(v, cfg1, cfg2)

        return v
                    
    def scale(self, dcfg, k):
        """ Scale a velocity by k """
##        print 'scale', dcfg
        new = self.copy(dcfg)
        for p in self.params:
            if isinstance(p, manipulator.PermutationParameter):                   
##                print 'scale', dcfg[p.name]
                new[p.name]=p.scale_swaps(new[p.name], k)
##                print 'to', dcfg[p.name]
            else:
                p.scale(new, k)
##        print 'to', new
        return new

    def split(self, dcfg, k):
        new1 = self.copy(dcfg)
        new2 = self.copy(dcfg)
        for p in self.params:
            if isinstance(p, manipulator.PermutationParameter):                   
##                print 'scale', dcfg[p.name]
                new1[p.name], new2[p.name]=p.split_swaps(dcfg[p.name], k)
##                print 'to', dcfg[p.name]
            else:
                pass
##        print 'to', new
        return new1, new2           

    def sum_v(self, *vs):
        """ Return the sum of a list of velocities """
        vsum= {}
        for p in self.params:
            if isinstance(p, manipulator.PermutationParameter):
                vsum[p.name] = p.sum_swaps(*[v[p.name] for v in vs])
            else:
                p.sum(vsum, *vs)       
        return vsum
        
        

    def add_v(self, cfg, v):
        """ Add a velocity to the position """
        new = self.copy(cfg)
        for p in self.params:
            if isinstance(p, manipulator.PermutationParameter):
                p.apply_swaps(v[p.name], new)
            else:
                p.sum(new, v, cfg)

        return new
        

    def crossover(self, cfg1, cfg2):
        for p in self.params:
            if isinstance(p, manipulator.PermutationParameter):
##                new = p.OX1(cfg1, cfg2, 3)
                new = p.OX3(cfg1, cfg2, 5)
##                new = p.PX(cfg1, cfg2)
##                new = p.EX(cfg1, cfg2)
##                new = p.CX(cfg1, cfg2)
            else:
                # crossover undefined for non-permutations
                pass 
            if len(new)>1:    # the offspring from cfg1
                new = new[0]
        return new        
        
        
        
class OXMixin(object):

  def crossover(self, cfgs):
    '''
    Crossover the first permtation parameter, if found, of two parents and
    return one offspring cfg
    '''
    
    cfg1, cfg2, = cfgs
    params = self.manipulator.parameters(cfg)
    for param in params:
      if param.is_permutation():
        new = param.OX3(cfg1, cfg2)[0]  # TODO: take both offsprings
        return new
    return cfg1

       
                                
                                

technique.register(PSO(name='pso-ox'))
