import {Component, OnInit, OnDestroy} from '@angular/core';
import {DisplaysService} from '../displays.service';
import {of, Subscription, Observable, BehaviorSubject} from 'rxjs';
import {Device} from '../device-interface';

@Component({
  selector: 'app-view-pane',
  templateUrl: './view-pane.component.html',
  styleUrls: ['./view-pane.component.sass'],
})
export class ViewPaneComponent implements OnInit, OnDestroy {
  deviceURL = '';
  visibleDevices = new BehaviorSubject<Device[]>([]);
  private subscription!: Subscription;

  constructor(
    public displaysService: DisplaysService,
  ) {}

  trackById(index : number, device : Device) {
    return device.deviceId;
  }

  ngOnInit(): void {
    this.displaysService
      .getVisibleDevices()
      .subscribe(display => {
        this.visibleDevices.next(display);
      });
  }

  ngOnDestroy(): void {
    this.subscription.unsubscribe();
  }
}
