import {Component, OnInit, OnDestroy} from '@angular/core';
import {DisplaysService} from '../displays.service';
import {Subscription} from 'rxjs';

@Component({
  selector: 'app-view-pane',
  templateUrl: './view-pane.component.html',
  styleUrls: ['./view-pane.component.sass'],
})
export class ViewPaneComponent implements OnInit, OnDestroy {
  deviceURL = '';
  visibleDevices: string[] = [];
  private subscription!: Subscription;

  constructor(
    public displaysService: DisplaysService,
  ) {}

  ngOnInit(): void {
    this.subscription = this.displaysService
      .getVisibleDevices()
      .subscribe(display => {
        this.visibleDevices = display;
      });
  }

  ngOnDestroy(): void {
    this.subscription.unsubscribe();
  }
}
