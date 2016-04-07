"""Add contact_expiration_check_interval to ps_globals

Revision ID: 5813202e92be
Revises: 3bcc0b5bc2c9
Create Date: 2016-03-08 21:52:21.372310

"""

# revision identifiers, used by Alembic.
revision = '5813202e92be'
down_revision = '3bcc0b5bc2c9'

from alembic import op
import sqlalchemy as sa

def upgrade():
    op.add_column('ps_globals', sa.Column('contact_expiration_check_interval', sa.Integer))

def downgrade():
    op.drop_column('ps_globals', 'contact_expiration_check_interval')
