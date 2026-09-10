import unittest
from mechbot_pwm_feedback import measure, recommend, KEYS, WHEELS

class FeedbackTests(unittest.TestCase):
    def samples(self):
        return [dict(elapsed=i*.2, ms=i*200, counts=[i*20,i*30,i*40,i*20]) for i in range(25)]
    def test_calibration_and_bounded_trim(self):
        data=measure(self.samples(),dict(zip(WHEELS,[100,150,100,100])),[1]*4)
        self.assertTrue(data['valid'])
        self.assertEqual(data['rpm'],dict(zip(WHEELS,[60,60,120,60])))
        rec=recommend(dict.fromkeys(KEYS,177),data)
        self.assertEqual(list(rec['deltas'].values()),[0,0,-5,0])
        self.assertEqual(recommend(dict.fromkeys(KEYS,177),data,heading_on=True)['kind'],'hold')
    def test_startup_excluded_reverse_and_short_run(self):
        samples=self.samples()
        for s in samples[:5]: s['counts']=[999999]*4
        self.assertTrue(measure(samples,dict.fromkeys(WHEELS,100),[1]*4)['valid'])
        self.assertFalse(measure(samples[:8],dict.fromkeys(WHEELS,100),[1]*4)['valid'])
        for s in samples: s['counts']=[-n for n in s['counts']]
        self.assertTrue(measure(samples,dict.fromkeys(WHEELS,100),[-1]*4)['valid'])
        self.assertFalse(measure(samples,dict.fromkeys(WHEELS,100),[1]*4)['valid'])
    def test_gaps_stalls_and_missing_calibration(self):
        samples=self.samples(); samples[12]['ms']+=1000
        self.assertFalse(measure(samples,dict.fromkeys(WHEELS,100),[1]*4)['valid'])
        samples=self.samples()
        for s in samples: s['counts'][0]=0
        self.assertFalse(measure(samples,dict.fromkeys(WHEELS,100),[1]*4)['valid'])
        self.assertFalse(measure(self.samples(),{},[1]*4)['valid'])
        self.assertEqual(recommend(dict.fromkeys(KEYS,177),{})['kind'],'hold')

    def test_heading_wrap_and_stale(self):
        import math
        from mechbot_pwm_feedback import heading_result
        samples=[]
        for i,angle in enumerate([179,-179,-177]):
            now=10+i*.3
            samples.append(dict(time=now,imu_updated=now,imu=f"IR2 {i*300} 0 READY {-angle} 0 0 0 0 0 0 0 0 0 {math.radians(angle)}"))
        result=heading_result(samples)
        self.assertTrue(result['valid'])
        self.assertAlmostEqual(result['change_deg'],4)
        samples[1]['imu_updated']=1
        self.assertFalse(heading_result(samples)['valid'])
