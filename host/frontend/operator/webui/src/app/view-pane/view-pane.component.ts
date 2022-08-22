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
  visibleDevices = this.displaysService.getVisibleDevices();

  constructor(
    public displaysService: DisplaysService,
  ) {}

  ngOnInit(): void {}

  ngOnDestroy(): void {}
}
